/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/xen/console.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/console/console.h>
#if defined(CONFIG_BSD_LIBRARY)
#include <modem/bsdlib.h>
#include <bsd.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#endif /* CONFIG_BSD_LIBRARY */
#include <net/nrf_cloud.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/net_if.h>
#if defined(CONFIG_NRF_CLOUD_AGPS)
#include <net/nrf_cloud_agps.h>
#endif /* CONFIG_NRF_CLOUD_AGPS */
#include <zephyr/logging/log.h>
#if defined(CONFIG_LWM2M_CARRIER)
#include <lwm2m_carrier.h>
#endif /* CONFIG_LWM2M_CARRIER */
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <zephyr/dfu/mcuboot.h>
#endif /* CONFIG_BOOTLOADER_MCUBOOT */
#include <zephyr/drivers/pwm.h>
#include <modem/nrf_modem_lib.h>
#include <modem/at_monitor.h>
#include <modem/modem_info.h>
#include <modem/lte_lc.h>
#include "watchdog_app.h"
#include "gps_controller.h"

#include "mqtt/mqtt.h"
#include "mqtt/config.h"
#include "hal/hal_adc.h"
#include "hal/hal_gpio.h"
#include "nvs/local_storage.h"
#include "bme/bme680_helper.h"
#include "modem/modem_helper.h"
#include "icm/icm_chip_helper.h"
#include "ecdsa.h"
#include "light_sensor/tsl2572.h"
#include  "mqtt/devReg.h"
#include "display.h"
#include "ver.h"
#include "keyBoard.h"
#include "icm42605/icm426xx_pedometer.h"

#include "psa/crypto.h"
#include "include/jose/jose.h"
#include "include/dids/dids.h"

#include "deviceregister.h"
#include "sprout.h"

/* change this to any other UART peripheral if desired */
// #define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)

// static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

// #define MSG_SIZE 256

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
// K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 2, 4);

/* receive buffer used in UART ISR callback */
// static char rx_buf[MSG_SIZE];
// static int rx_buf_pos;

#if !defined(CONFIG_USE_PROVISIONED_CERTIFICATES)
#if defined(CONFIG_MODEM_KEY_MGMT)
#include <modem/modem_key_mgmt.h>
#endif /* CONFIG_MODEM_KEY_MGMT */
#endif /* CONFIG_USE_PROVISIONED_CERTIFICATES */

LOG_MODULE_REGISTER(Riverrock_main, CONFIG_ASSET_TRACKER_LOG_LEVEL);

#if defined(CONFIG_BSD_LIBRARY) && !defined(CONFIG_LTE_LINK_CONTROL)
#error "Missing CONFIG_LTE_LINK_CONTROL"
#endif /* if defined(CONFIG_BSD_LIBRARY) && !defined(CONFIG_LTE_LINK_CONTROL) */

#if defined(CONFIG_BSD_LIBRARY) && defined(CONFIG_LTE_AUTO_INIT_AND_CONNECT) && defined(CONFIG_NRF_CLOUD_PROVISION_CERTIFICATES)
#error "PROVISION_CERTIFICATES \
    requires CONFIG_LTE_AUTO_INIT_AND_CONNECT to be disabled!"
#endif /* if defined(CONFIG_BSD_LIBRARY) && defined(CONFIG_LTE_AUTO_INIT_AND_CONNECT) && defined(CONFIG_NRF_CLOUD_PROVISION_CERTIFICATES) */

#define SUCCESS_OR_BREAK(rc) { if (rc != 0) { return ; } }

#if(CONFIG_IOTEX_BOARD_VERSION == 3)
Z_GENERIC_SECTION(.openocd_dbg.5) __attribute__((used)) const  uint8_t AppVersion[]= APP_VERSION_INFO;
#elif(CONFIG_IOTEX_BOARD_VERSION == 2)
Z_GENERIC_SECTION(.openocd_dbg.5) __attribute__((used)) const  uint8_t AppVersion[]=APP_VERSION_INFO;
#else
Z_GENERIC_SECTION(.openocd_dbg.5) __attribute__((used)) const  uint8_t AppVersion[]=APP_VERSION_INFO;
#endif /* CONFIG_IOTEX_BOARD_VERSION == 3 */

#define IOTEX_PEBBLE_SENSOR_DATA_BUFFER_SIZE            512

#define IOTEX_PEBBLE_FEATURE_MODEM_DAMON_ENABLE

#ifdef IOTEX_PEBBLE_FEATURE_MODEM_DAMON_ENABLE
#define IOTEX_PEBBLE_PUBLISH_SENSOR_DATA_COUNT_CONTINUE
#endif

/* Stack definition for application workqueue */
K_THREAD_STACK_DEFINE(application_stack_area,CONFIG_APPLICATION_WORKQUEUE_STACK_SIZE);
static struct k_work_q application_work_q;
const uint8_t firmwareVersion[] = IOTEX_APP_VERSION;
/* File descriptor */
static struct pollfd fds;
/* MQTT Broker details. */
static struct mqtt_client client;
static atomic_val_t stopAnimation = ATOMIC_INIT(0);
static atomic_val_t keyWaitFlg = ATOMIC_INIT(0);
static k_tid_t mainThreadID;
static atomic_val_t pebbleStartup = ATOMIC_INIT(1);
extern atomic_val_t modemWriteProtect;
/* Structures for work */
static struct k_work_delayable send_env_data_work;
static struct k_work_delayable   animation_work;
#ifdef IOTEX_PEBBLE_FEATURE_MODEM_DAMON_ENABLE
static struct k_work_delayable   modem_damon_work;
#endif
enum error_type {
    ERROR_CLOUD,
    ERROR_BSD_RECOVERABLE,
    ERROR_LTE_LC,
    ERROR_SYSTEM_FAULT
};
static char mqttPubBuf[DATA_BUFFER_SIZE];
const char reconnectReminder[][4] = {
    "",
    "1st",
    "2nd",
    "3rd"
};
static void work_init(void);
static void periodic_publish_sensors_data(void);
static void sampling_and_store_sensor_data(void);

#if defined(CONFIG_NRF_MODEM_LIB)
NRF_MODEM_LIB_ON_INIT(pebble_init_hook, on_modem_lib_init, NULL);

/* Initialized to value different than success (0) */
static int modem_lib_init_result = -1;
static int _ioID_send_message_err = 0;

#ifdef IOTEX_PEBBLE_FEATURE_MODEM_DAMON_ENABLE  
static int modem_damon_inited = 0;
#endif

static void on_modem_lib_init(int ret, void *ctx)
{
	modem_lib_init_result = ret;
}
#endif /* CONFIG_NRF_MODEM_LIB */
static const char *modem_crash_reason_get(uint32_t reason)
{
	switch (reason) {
	case NRF_MODEM_FAULT_UNDEFINED:
		return "Undefined fault";

	case NRF_MODEM_FAULT_HW_WD_RESET:
		return "HW WD reset";

	case NRF_MODEM_FAULT_HARDFAULT:
		return "Hard fault";

	case NRF_MODEM_FAULT_MEM_MANAGE:
		return "Memory management fault";

	case NRF_MODEM_FAULT_BUS:
		return "Bus fault";

	case NRF_MODEM_FAULT_USAGE:
		return "Usage fault";

	case NRF_MODEM_FAULT_SECURE_RESET:
		return "Secure control reset";

	case NRF_MODEM_FAULT_PANIC_DOUBLE:
		return "Error handler crash";

	case NRF_MODEM_FAULT_PANIC_RESET_LOOP:
		return "Reset loop";

	case NRF_MODEM_FAULT_ASSERT:
		return "Assert";

	case NRF_MODEM_FAULT_PANIC:
		return "Unconditional SW reset";

	case NRF_MODEM_FAULT_FLASH_ERASE:
		return "Flash erase fault";

	case NRF_MODEM_FAULT_FLASH_WRITE:
		return "Flash write fault";

	case NRF_MODEM_FAULT_POFWARN:
		return "Undervoltage fault";

	case NRF_MODEM_FAULT_THWARN:
		return "Overtemperature fault";

	default:
		return "Unknown reason";
	}
}
void nrf_modem_fault_handler(struct nrf_modem_fault_info *fault_info)
{
	printk("Modem crash reason: 0x%x (%s), PC: 0x%x\n",
		fault_info->reason,
		modem_crash_reason_get(fault_info->reason),
		fault_info->program_counter);

	__ASSERT(false, "Modem crash detected, halting application execution");
}

/**@brief nRF Cloud error handler. */
void error_handler(enum error_type err_type, int err_code)
{
    if (err_type == ERROR_CLOUD) {
        if (mqtt_disconnect(&client)) {
            LOG_ERR("Could not disconnect MQTT client during error handler.\n");
        }
        k_work_cancel_delayable(&send_env_data_work);
        return;
    }
}

/*  Upload sensor data */
static void uploadSensorData(void) {
    if (!atomic_get(&send_data_enable) || atomic_get(&pebbleStartup)) {
        return;
    }
    if (iotex_mqtt_is_bulk_upload()) {
        sampling_and_store_sensor_data();
    }
    else {
        periodic_publish_sensors_data();
    }
    pubOnePack();
}

/*  Restore work queue */
void RestartEnvWork(int s) {
    k_work_cancel_delayable(&send_env_data_work);
    k_work_reschedule(&send_env_data_work, K_SECONDS(s));
}

/*  mqtt cert write into modem */
void WriteCertIntoModem(uint8_t *cert, uint8_t *key, uint8_t *root ) {
    uint8_t *certificates[] = {root, key, cert};
    size_t cert_len[] = { strlen(root), strlen(key), strlen(cert) };
    int err;
    sec_tag_t sec_tag = CONFIG_CLOUD_CERT_SEC_TAG;
    enum modem_key_mgmt_cred_type cred[] = {
        MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
        MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT,
        MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT,
    };
    /* wait for modem flash writing over */
    if(atomic_get(&modemWriteProtect))
        return;

    disableModem();
    /* Delete certificates */
    for (enum modem_key_mgmt_cred_type type = 0; type < 3; type++) {
        err = modem_key_mgmt_delete(sec_tag, type);
        LOG_ERR("modem_key_mgmt_delete(%u, %d) => result=%d\n",
                sec_tag, type, err);
    }

    /* Write certificates */
    for (enum modem_key_mgmt_cred_type type = 0; type < 3; type++) {
        err = modem_key_mgmt_write(sec_tag, cred[type],
                certificates[type], cert_len[type]);
        LOG_ERR("modem_key_mgmt_write => result=%d\n", err);
    }
}

/*  Upload sensor data regularly */
static void periodic_publish_sensors_data(void) {
    int rc;
    SUCCESS_OR_BREAK(mqtt_ping(&client));
    if (rc = SensorPackage(iotex_mqtt_get_data_channel(), mqttPubBuf)) {
        // rc = iotex_mqtt_publish_data(&client, 0, mqttPubBuf, rc);           // TODO : Send Sensor Data
        LOG_INF("mqtt_publish_devicedata: %d \n", rc);
    } else {
        LOG_ERR("mqtt package error ! \n");
    }
}

static void bulk_publish_sersor_data(void) {
    int rc;
    LOG_INF("[%s:%d]\n", __func__, __LINE__);        
    while(!iotex_mqtt_is_bulk_upload_over()) {    
        rc = get_block_size();
        iotex_local_storage_hist(SID_MQTT_BULK_UPLOAD_DATA, mqttPubBuf, rc, get_his_block());
        // rc = iotex_mqtt_publish_data(&client, 0, mqttPubBuf, rc);           // TODO : Send Sensor Data
        LOG_INF("mqtt_bulk_upload: %d \n", rc);
        iotex_mqtt_inc_current_upload_count();
    }  
}

/*  Upload sensor data regularly */
static void periodic_publish_sensors_data_ioid(void) {
    
    int rc;
    char sensor_data[IOTEX_PEBBLE_SENSOR_DATA_BUFFER_SIZE] = {0};

    uint16_t channel = iotex_mqtt_get_data_channel();
    LOG_INF("channel : %d", channel);
    
    memset(mqttPubBuf, 0, sizeof(mqttPubBuf));

    if (rc = SensorPackage(channel, mqttPubBuf)) {
        
        LOG_INF("SensorPackage Length : %d", rc);
        sensor_data[0] = '0';
        sensor_data[1] = 'x';
        iotex_utils_convert_hex_to_str(mqttPubBuf, rc, sensor_data + 2); 
#ifdef IOTEX_PEBBLE_SENSOR_DATA_DISPLAY_ENABLE        
        uint8_t mqttPubBuf_str[1024] = {0};
        iotex_utils_convert_hex_to_str(mqttPubBuf, rc, mqttPubBuf_str);
        printf("mqttPubBuf_str : %s\n", mqttPubBuf_str);
#endif        
        // if (sensor_data && (0 == iotex_pal_sprout_didcomm_send_message(sensor_data, true))) {
        if ( 0 == iotex_pal_sprout_didcomm_send_message(sensor_data, true) ) {
            LOG_INF("Success to Send Sensor Package : %d \n", rc);
            
            pubOnePack();

            _ioID_send_message_err = 0;

        } else {
            _ioID_send_message_err++;
            LOG_ERR("Failed to send Sensor Package : %d", _ioID_send_message_err);
        }

    } else {
        _ioID_send_message_err++;
        LOG_ERR("Failed to get Sensor Package : %d", _ioID_send_message_err);
    }

    // if (sensor_data)
    //     free (sensor_data);
}

#if 0
int publish_dev_ownership(char *buf, int len) {
    return iotex_mqtt_publish_ownership(&client, 0, buf, len);
}

/*  publish query package */
int publish_dev_query(char *buf, int len) {
    return iotex_mqtt_publish_query(&client, 0, buf, len);
}
#endif

void animation_work_fn(struct k_work *work) {
    if (atomic_get(&stopAnimation)) {
        return;
    }

    sta_Refresh();
    k_work_schedule_for_queue(&application_work_q, &animation_work, K_SECONDS(1));
}

#ifdef IOTEX_PEBBLE_FEATURE_MODEM_DAMON_ENABLE  

static uint32_t _pubcount_old       = 0;
static uint32_t _pubcount_unchanged = 0;

void modem_damon_work_fn(struct k_work *work) {

    uint32_t _pubcount_new = getPubCount();

    LOG_INF("Modem Damon PubCount[New] : %d", _pubcount_new);
    LOG_INF("Modem Damon PubCount[Old] : %d", _pubcount_old);

    if ( 0 == _pubcount_new) {
        goto exit;
    }

    if ( 0 == _pubcount_old) {
        _pubcount_old = _pubcount_new;
        _pubcount_unchanged = 0;

        goto exit;
    }

    if ( _pubcount_old == _pubcount_new) {
        _pubcount_unchanged++;
    } else {
        _pubcount_old = _pubcount_new;
        _pubcount_unchanged = 0;
    }

    LOG_INF("Modem Damon Unchanged : %d", _pubcount_unchanged);

    if (_pubcount_unchanged > 5) {

#ifdef IOTEX_PEBBLE_PUBLISH_SENSOR_DATA_COUNT_CONTINUE
        iotex_local_storage_save(SID_PUB_COUNT, &_pubcount_new, 4);
#endif        

        sys_reboot(0);
    }

exit:
    k_work_schedule_for_queue(&application_work_q, &modem_damon_work, K_SECONDS(120));
}
#endif

void stopAnimationWork(void) {
    atomic_set(&stopAnimation, 1);
}

void startAnimationWork(void) {
    atomic_set(&stopAnimation, 0);
}

/**@brief Initializes and submits delayed work. */
static void work_init(void) {
    k_work_init_delayable(&animation_work, animation_work_fn);
#ifdef IOTEX_PEBBLE_FEATURE_MODEM_DAMON_ENABLE    
    k_work_init_delayable(&modem_damon_work, modem_damon_work_fn);
#endif
}

#ifdef IOTEX_PEBBLE_PUBLISH_SENSOR_DATA_COUNT_CONTINUE
static void pubSensorDataCountInit(void) {

    uint32_t pubcount = 0;

    int ret = iotex_local_storage_load(SID_PUB_COUNT, &pubcount, 4);
    if ( -1 == ret)
        return;

    LOG_INF("Load PubCount %d", pubcount);

    if (pubcount) {
        setPubCount(pubcount);
        int temp = 0;
        iotex_local_storage_save(SID_PUB_COUNT, &temp, 4);
    }
}
#endif

/**@brief Configures modem to provide LTE link. Blocks until link is
 * successfully established.
 */
static void modem_configure(void) {
    int err;

    LOG_INF("Connecting to LTE network. ");
    LOG_INF("This may take several minutes.\n");  
    err = lte_lc_psm_req(true);
    if (err) {
        LOG_ERR("lte_lc_psm_req erro:%d\n", err);
        error_handler(ERROR_LTE_LC, err);
    }
    err = lte_lc_init_and_connect();
    if (err) {
        lte_lc_deinit();
        LOG_INF("modem fallback\n");
        anotherWorkMode();
        err = lte_lc_init_and_connect();
        if (err) {
            LOG_ERR("LTE link could not be established, system will reboot.\n");
            sys_reboot(0);
        }
    }        
    LOG_INF("Connected to LTE network\n");
    /* ui_led_active(LTE_CONNECT_MASK,1); */
    sta_SetMeta(LTE_LINKER, STA_LINKER_ON);
}

void handle_bsdlib_init_ret(void) {
#if defined(CONFIG_BSD_LIBRARY)
    int ret = bsdlib_get_init_ret();

    /* Handle return values relating to modem firmware update */
    switch (ret) {
    case MODEM_DFU_RESULT_OK:
        LOG_INF("MODEM UPDATE OK. Will run new firmware");
        sys_reboot(SYS_REBOOT_COLD);
        break;
    case MODEM_DFU_RESULT_UUID_ERROR:
    case MODEM_DFU_RESULT_AUTH_ERROR:
        LOG_ERR("MODEM UPDATE ERROR %d. Will run old firmware", ret);
        sys_reboot(SYS_REBOOT_COLD);
        break;
    case MODEM_DFU_RESULT_HARDWARE_ERROR:
    case MODEM_DFU_RESULT_INTERNAL_ERROR:
        LOG_ERR("MODEM UPDATE FATAL ERROR %d. Modem failiure", ret);
        sys_reboot(SYS_REBOOT_COLD);
        break;
    default:
        break;
    }
#endif /* CONFIG_BSD_LIBRARY */
}

/*  Sample and store to nv flash */
static void sampling_and_store_sensor_data(void) {
    /* Data sampling mode */ 
    if (iotex_mqtt_is_need_sampling()) {
        /* Sampling data and save to nvs,
        when required sampling count fulfilled, start bulk upload.
        Support breakpoint resampling and breakpoint retransmission.
        */
        LOG_INF("Before...............\n");
        if (!iotex_mqtt_sampling_data_and_store(iotex_mqtt_get_data_channel())) {
            LOG_ERR("[%s:%d] Sampling and store data failed!\n", __func__, __LINE__);
            return;
        }
#ifdef CONFIG_DEBUG_MQTT_CONFIG
        LOG_INF("[%s:%d]: Sampling count: %u\n", __func__, __LINE__, iotex_mqtt_get_current_sampling_count() + 1);
#endif

        /* Required sampling count is fulfilled */
        if (iotex_mqtt_inc_current_sampling_count()) {
        }
    }
    /* Data upload mode */
    else {
        bulk_publish_sersor_data();
    }
}

void stopMqtt(void) {
    mqtt_disconnect(&client);
}
void keyWakeup(void) {
    if((devRegGet() == DEV_REG_STOP) && (!atomic_get(&keyWaitFlg))) {
        if (isComninationKeys(KB_UP_KEY|KB_DOWN_KEY)) {
            atomic_set(&keyWaitFlg,1);
            k_wakeup(mainThreadID);
            ctrlOLED(true);
        }
    }
}

bool isKeyWakeFlg(void) {
    return (atomic_get(&keyWaitFlg));
}
/*
    time  unit second
*/
int  eventPolling(unsigned int time, struct pollfd *fds, struct mqtt_client *client) {
    unsigned int waitCounts = time * 1000/CONFIG_MAIN_BASE_TIME;
    int err = 0;

    while (waitCounts--) {
        err = poll(fds, 1, CONFIG_MAIN_BASE_TIME);
        if (err < 0) {
            LOG_ERR("ERROR: poll %d\n", errno);
            err = -1;
            break;
        }
        if ((fds->revents & POLLIN) == POLLIN) {
            err = mqtt_input(client);
            if (err != 0) {
                LOG_ERR("ERROR: mqtt_input %d\n", err);
                err = -1;
                break;
            }
        }
        if ((fds->revents & POLLERR) == POLLERR) {
            LOG_ERR("POLLERR\n");
            err = -1;
            break;
        }
        if ((fds->revents & POLLNVAL) == POLLNVAL) {
            LOG_ERR("POLLNVAL\n");
            err = -1;
            break;
        }
        err = mqtt_live(client);
        if (err != 0 && err != -EAGAIN) {
            LOG_ERR("ERROR: mqtt_live %d\n", err);
            err = -1;
            break;
        }   
    }
    return  err;
}

int psmWork(void) {
    int ret;

#if 0
    if(atomic_get(&keyWaitFlg)){ 
        LOG_INF("wakeup and waiting for download\n");
        hintString(htstartReconf, HINT_TIME_FOREVER);
        eventPolling(300, &fds, &client);   
        atomic_set(&keyWaitFlg,0);
    }
#endif

    LOG_INF("Upload Sensor Data");
#if 0
    uploadSensorData();
    ret = k_sleep(K_MSEC(200));
    if(ret) {
        if(atomic_get(&keyWaitFlg)){
            LOG_INF("wakeup after data pubs\n");
            hintString(htstartReconf, HINT_TIME_FOREVER);
            eventPolling(300, &fds, &client);
            atomic_set(&keyWaitFlg,0);
        }
    }
    mqtt_disconnect(&client);    
#else
    periodic_publish_sensors_data_ioid();
    k_sleep(K_MSEC(200));
    iotex_pal_sprout_http_server_disconnect();
#endif

    if(iotex_mqtt_get_upload_period() > 30)
        gpsSleep();
    LOG_INF("Start Sleep\n");
    setModemSleep(1);
    ret = k_sleep(K_SECONDS(1));
    if(ret) {
        if(atomic_get(&keyWaitFlg)){
            LOG_INF("wakeup after http disconnect\n"); 
            hintString(htstartMqtt, HINT_TIME_FOREVER);
            lte_lc_psm_req(false);
            return 1;
        }
    }
    lte_lc_psm_req(true);
#if 1    
    if(iotex_mqtt_get_upload_period() > 30)
        ret = k_sleep(K_SECONDS(iotex_mqtt_get_upload_period() - getSatelliteSearchingTime()));
    else
        ret = k_sleep(K_SECONDS(iotex_mqtt_get_upload_period() - 1));
#else
    ret = k_sleep(K_SECONDS(5));
#endif        
    if(ret) {
        if(atomic_get(&keyWaitFlg)){
            LOG_INF("wakeup after psm\n"); 
            hintString(htstartMqtt, HINT_TIME_FOREVER);
            lte_lc_psm_req(false);
            return 1;
        }
    }
    gpsWakeup();
    if(iotex_mqtt_get_upload_period() > 30) {
        ret = searchingSatelliteTime();  
        if(atomic_get(&keyWaitFlg)){
            LOG_INF("wakeup after gps start\n");
            hintString(htstartMqtt, HINT_TIME_FOREVER);
            lte_lc_psm_req(false);
            return 1;
        } 
    }
    lte_lc_psm_req(false);  
    LOG_INF("wake up\n"); 
    setModemSleep(0);
    atomic_set(&pebbleStartup,0);
    return 0;   
}

int iotex_ioconnect_pal_init(JWK* signJWK)
{
    psa_crypto_init();
    
    int ret =  iotex_pal_jose_generate_jwk(signJWK);
    char *deviceDID = iotex_pal_jose_device_did_get();
    char *deviceKA_KID = iotex_pal_jose_device_kakid_get();

    iotex_pal_sprout_init(deviceDID, deviceKA_KID);

    return 0;    
}

void main(void) {
    int err, errCounts = 0, errHttpConnect = 0;

	err = nrf_modem_lib_init();
	if (err < 0) {
		LOG_ERR("Modem library init failed, err: %d", err);
		return err;
	}
    LOG_INF("APP %s  %s started\n", IOTEX_APP_NAME, RELEASE_VERSION);
    mainThreadID = k_current_get(); 
    k_work_queue_start(&application_work_q, application_stack_area,K_THREAD_STACK_SIZEOF(application_stack_area),K_LOWEST_APPLICATION_THREAD_PRIO, NULL);
    /*  open watchdog */
    if (IS_ENABLED(CONFIG_WATCHDOG)) {
        watchdog_init_and_start();
    }
    /*   init ECDSA */
    /*  i2c speed 400kbps  */
    setI2Cspeed(2);
    /* HAL init, notice gpio must be the first (to set IO_POWER_ON on )*/
    iotex_local_storage_init();
    /*  read ecdsa key pair */
#ifdef IOTEX_PAL_CRYPT_USE_IOID    
    JWK* deviceJWK = iotex_pal_crypt_init();
    if (NULL == deviceJWK) {
        LOG_ERR("Fail to generate a master JWK");
        LOG_ERR("System will not startup");
        return;
    }

    iotex_ioconnect_pal_init(deviceJWK);

#else
    if (err = iotex_pal_crypt_init()) {
        LOG_ERR("check ecc key error: %d ", err);
        LOG_ERR("system will not startup");
        return;
    }
#endif    
    /*  init gpio */
    iotex_hal_gpio_init();
    /*  init  onchip adc */
    iotex_hal_adc_init();
    /*  init GPS */
    exGPSInit();
    /* Iotex Init BME680 */
    iotex_bme680_init();
    /* Iotex Init TSL2572 */
    iotex_TSL2572_init(GAIN_1X); 
    /*  iotex keyboard */
    iotex_key_init();
    /*  init worker queue */
    work_init();
    initOTA();
    /*  init oled */
    ssd1306_init();
    /* Iotex Init ICM42605 */
    iotex_icm_chip_init();
    /*  system  menu */
    MainMenu();
    /*  OTA upgrade  */
    appEntryDetect();
    /*  work queue of the status bar  */
    if (!get_ota_process_status()) {
        k_work_schedule_for_queue(&application_work_q, &animation_work, K_MSEC(10));
        k_work_schedule_for_queue(&application_work_q, &modem_damon_work, K_MSEC(30));
    }
    
    /*  LTE-M / NB-IOT network attach */
    modem_configure();
    handle_bsdlib_init_ret();
    /*  status bar refresh */

#if 0  
    if (!get_ota_process_status()) {
        sta_Refresh();
    }
      
    initNTP();
#endif

    iotex_pal_sprout_didcomm_prepare(); 
        
    appEntryOTAProcess();
    
    /*  status bar refresh */
    sta_Refresh();

#ifdef IOTEX_PEBBLE_PUBLISH_SENSOR_DATA_COUNT_CONTINUE
    pubSensorDataCountInit();
#endif

    int ret = 0;
exit:
    while (true) {
        ret = iotex_pal_sprout_is_ready_check();
        switch (ret) {
            case IOTEX_PAL_SPROUT_DEVICE_READY_NOT_REGISTER:
                hintString(deviceRegister, HINT_TIME_FOREVER);
                break;
#ifdef IOTEX_SPROUT_COMMUNICATE_PROTOCOL_USE_DIDCOMM             
            case IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_DIDDOC:
                hintString(getDiddocErr, HINT_TIME_FOREVER);
                break;
            case IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_TOKEN:
                hintString(getTokenErr, HINT_TIME_FOREVER);
                break;                                        
#endif
            case IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_PUBKEY:
            case IOTEX_PAL_SPROUT_DEVICE_READY_FAIL_GET_TIMESTAMP:
                hintString(getServerInfoErr, HINT_TIME_FOREVER);
                break;                                        
            default:
                break;
        }

        if (ret) {
            k_sleep(K_MSEC(5000));
            continue;
        }

        if (IOTEX_SPROUT_ERR_SUCCESS != iotex_pal_sprout_http_server_connect()) {
            hintString(httpConnectErr, HINT_TIME_FOREVER);

            k_sleep(K_MSEC(5000));

            if (++errHttpConnect > 5)
                break;

            continue;
        }

        errHttpConnect = 0;

        psmWork();

        if (_ioID_send_message_err > 3)
            break;
    }

    LOG_INF("System will reboot ...\n");

    iotex_hal_gpio_set(LED_RED, LED_ON);
    k_sleep(K_MSEC(500));
    iotex_hal_gpio_set(LED_RED, LED_OFF);
    k_sleep(K_MSEC(500));
    iotex_hal_gpio_set(LED_RED, LED_OFF);
    k_sleep(K_MSEC(500));
    sys_reboot(0);
}

