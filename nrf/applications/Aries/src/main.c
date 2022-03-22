/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
#include <zephyr.h>
#include <kernel_structs.h>
#include <stdio.h>
#include <string.h>
#include <drivers/gps.h>
#include <drivers/sensor.h>
#include <console/console.h>
#include <power/reboot.h>
#include <logging/log_ctrl.h>
#if defined(CONFIG_BSD_LIBRARY)
#include <modem/bsdlib.h>
#include <bsd.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#endif /* CONFIG_BSD_LIBRARY */
#include <net/cloud.h>
#include <net/socket.h>
#include <net/nrf_cloud.h>
#if defined(CONFIG_NRF_CLOUD_AGPS)
#include <net/nrf_cloud_agps.h>
#endif /* CONFIG_NRF_CLOUD_AGPS */
#include <logging/log.h>
#if defined(CONFIG_LWM2M_CARRIER)
#include <lwm2m_carrier.h>
#endif /* CONFIG_LWM2M_CARRIER */
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <dfu/mcuboot.h>
#endif /* CONFIG_BOOTLOADER_MCUBOOT */
#include <drivers/pwm.h>
#include "cloud_codec.h"
#include "env_sensors.h"
#include "motion.h"
#include "ui.h"
#include "service_info.h"
#include <modem/at_cmd.h>
#include "watchdog.h"
#include "gps_controller.h"

#include "mqtt/mqtt.h"
#include "mqtt/config.h"
#include "hal/hal_adc.h"
#include "hal/hal_gpio.h"
#include "nvs/local_storage.h"
#include "bme/bme680_helper.h"
#include "modem/modem_helper.h"
#include "icm/icm42605_helper.h"
#include "ecdsa.h"
#include "light_sensor/tsl2572.h"
#include  "mqtt/devReg.h"
#include "display.h"
#include "ver.h"
#include "keyBoard.h"
#include "icm/pedometer.h"
#if !defined(CONFIG_USE_PROVISIONED_CERTIFICATES)
#if defined(CONFIG_MODEM_KEY_MGMT)
#include <modem/modem_key_mgmt.h>
#endif /* CONFIG_MODEM_KEY_MGMT */
#endif /* CONFIG_USE_PROVISIONED_CERTIFICATES */

LOG_MODULE_REGISTER(Aries_main, CONFIG_ASSET_TRACKER_LOG_LEVEL);

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
//static atomic_val_t pebbleStartup = ATOMIC_INIT(1);
extern atomic_val_t modemWriteProtect;
/* Structures for work */
static struct k_delayed_work send_env_data_work;
static struct k_delayed_work   animation_work;
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

/**@brief nRF Cloud error handler. */
void error_handler(enum error_type err_type, int err_code)
{
    if (err_type == ERROR_CLOUD) {
        if (mqtt_disconnect(&client)) {
            LOG_ERR("Could not disconnect MQTT client during error handler.\n");
        }
        k_delayed_work_cancel(&send_env_data_work);
        return;
    }
}

/**@brief Recoverable BSD library error. */
void bsd_recoverable_error_handler(uint32_t err)
{
    error_handler(ERROR_BSD_RECOVERABLE, (int)err);
}

/*  Upload sensor data */
static void uploadSensorData(void) {
    if (!atomic_get(&send_data_enable)) {
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
    k_delayed_work_cancel(&send_env_data_work);
    k_delayed_work_submit(&send_env_data_work, K_SECONDS(s));
}

/*  mqtt cert write into modem */
void WriteCertIntoModem(uint8_t *cert, uint8_t *key, uint8_t *root ) {
    u8_t *certificates[] = {root, key, cert};
    size_t cert_len[] = { strlen(root), strlen(key), strlen(cert) };
    int err;
    sec_tag_t sec_tag = CONFIG_CLOUD_CERT_SEC_TAG;
    enum modem_key_mgnt_cred_type cred[] = {
        MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
        MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT,
        MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT,
    };
    /* wait for modem flash writing over */
    if(atomic_get(&modemWriteProtect))
        return;

    disableModem();
    /* Delete certificates */
    for (enum modem_key_mgnt_cred_type type = 0; type < 3; type++) {
        err = modem_key_mgmt_delete(sec_tag, type);
        LOG_ERR("modem_key_mgmt_delete(%u, %d) => result=%d\n",
                sec_tag, type, err);
    }

    /* Write certificates */
    for (enum modem_key_mgnt_cred_type type = 0; type < 3; type++) {
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
        rc = iotex_mqtt_publish_data(&client, 0, mqttPubBuf, rc);
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
        rc = iotex_mqtt_publish_data(&client, 0, mqttPubBuf, rc);
        LOG_INF("mqtt_bulk_upload: %d \n", rc);
        iotex_mqtt_inc_current_upload_count();
    }  
}

int publish_dev_ownership(char *buf, int len) {
    return iotex_mqtt_publish_ownership(&client, 0, buf, len);
}

/*  publish query package */
int publish_dev_query(char *buf, int len) {
    return iotex_mqtt_publish_query(&client, 0, buf, len);
}

void animation_work_fn(struct k_work *work) {
    if (atomic_get(&stopAnimation)) {
        return;
    }
    sta_Refresh();
    k_delayed_work_submit_to_queue(&application_work_q, &animation_work, K_SECONDS(1));
}

void stopAnimationWork(void) {
    atomic_set(&stopAnimation, 1);
}

/**@brief Initializes and submits delayed work. */
static void work_init(void) {
    k_delayed_work_init(&animation_work, animation_work_fn);
}

/**@brief Configures modem to provide LTE link. Blocks until link is
 * successfully established.
 */
static void modem_configure(void) {
#if defined(CONFIG_BSD_LIBRARY)

    if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
        /* Do nothing, modem is already turned on
         * and connected.
         */
    } else {
        int err;

        LOG_INF("Connecting to LTE network. ");
        LOG_INF("This may take several minutes.\n");  
        err = lte_lc_psm_req(true);
        if (err) {
            LOG_ERR("lte_lc_psm_req erro:%d\n", err);
            error_handler(ERROR_LTE_LC, err);
        }
        /* ui_led_set_pattern(UI_LTE_CONNECTING); */
        /* ui_led_deactive(LTE_CONNECT_MASK,1); */
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
#endif
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
            LOG_INF("start wakeup id:%d\n", mainThreadID);
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
    static uint64_t steps = 0;
    static enum PEDOMETER_CLASS_TYPE type = PEDOMETER_STOP;

    if(atomic_get(&keyWaitFlg)){ 
        LOG_INF("wakeup and waiting for download\n");
        hintString(htstartReconf, HINT_TIME_FOREVER);
        eventPolling(300, &fds, &client);
        atomic_set(&keyWaitFlg,0);
    }
    
    if(!stepsComp(steps, type)){
        LOG_INF("upload sensor data\n");
        uploadSensorData();
        steps = getSteps();
        type = getType();
    }
    else
        LOG_INF("No need to upload data\n");
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
    if(iotex_mqtt_get_upload_period() > 30)
        gpsSleep();
    LOG_INF("start sleep\n");
    setModemSleep(1);
    ret = k_sleep(K_SECONDS(1));
    if(ret) {
        if(atomic_get(&keyWaitFlg)){
            LOG_INF("wakeup after mqtt disconnect\n"); 
            hintString(htstartMqtt, HINT_TIME_FOREVER);
            lte_lc_psm_req(false);
            return 1;
        }
    }
    lte_lc_psm_req(true);
    do {
        if(iotex_mqtt_get_upload_period() > 30)
            ret = k_sleep(K_SECONDS(iotex_mqtt_get_upload_period()-getSatelliteSearchingTime()));
        else
            ret = k_sleep(K_SECONDS(iotex_mqtt_get_upload_period()-1));
        if(ret) {
            if(atomic_get(&keyWaitFlg)){
                LOG_INF("wakeup after psm\n");
                hintString(htstartMqtt, HINT_TIME_FOREVER);
                lte_lc_psm_req(false);
                return 1;
            }
        }
    }while (stepsComp(steps, type));
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
    //atomic_set(&pebbleStartup,0);
    return 0;   
}

void main(void) {
    int err, errCounts = 0;

    LOG_INF("APP %s  %s started", IOTEX_APP_NAME, RELEASE_VERSION);
    mainThreadID = k_current_get(); 
    k_work_q_start(&application_work_q, application_stack_area,K_THREAD_STACK_SIZEOF(application_stack_area),CONFIG_APPLICATION_WORKQUEUE_PRIORITY);
    /*  open watchdog */
    if (IS_ENABLED(CONFIG_WATCHDOG)) {
        watchdog_init_and_start(&application_work_q);
    }
    /*   init ECDSA */
    InitLowsCalc();
    if (initECDSA_sep256r()) {
        LOG_ERR("initECDSA_sep256r error\n");
        return;
    }
    /*  i2c speed 400kbps  */
    setI2Cspeed(2);
    /* HAL init, notice gpio must be the first (to set IO_POWER_ON on )*/
    iotex_local_storage_init();
    /*  read ecdsa key pair */
    if (startup_check_ecc_key()) {
        LOG_ERR("check ecc key error\n");
        LOG_ERR("system will not startup\n");
        return;
    }
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
    iotex_icm42605_init();
    /*  system  menu */
    MainMenu();
    /*  OTA upgrade  */
    appEntryDetect();
    /*  work queue of the status bar  */
    k_delayed_work_submit_to_queue(&application_work_q, &animation_work, K_MSEC(10));
    /*  LTE-M / NB-IOT network attach */
    modem_configure();
    handle_bsdlib_init_ret();
#ifdef CONFIG_UNITTEST
    unittest();
#endif
    /*  status bar refresh */
    sta_Refresh();

    while (true) {
        if ((err = iotex_mqtt_client_init(&client, &fds))) {
            errCounts++;
            if(errCounts < 4){
                LOG_ERR("**** %s reconnection ****\n",reconnectReminder[errCounts]);
                continue;
            }
            LOG_ERR("ERROR: mqtt_connect %d, rebooting...\n", err);
            k_sleep(K_MSEC(500));
            sys_reboot(0);
            break;
        }

        errCounts = 0;
        if (!iotexDevBinding(&fds,&client)) {
            if(psmWork())
                continue;
        }

        if (devRegGet() != DEV_REG_STOP) {
            devRegSet(DEV_REG_START);
            pebbleBackGround(0);
        }
    }

    /* connect_to_cloud(0); */
    LOG_INF("Disconnecting MQTT client...\n");
    err = mqtt_disconnect(&client);
    if (err) {
        LOG_ERR("Could not disconnect MQTT client. Error: %d\n", err);
    }
    iotex_hal_gpio_set(LED_RED, LED_ON);
    k_sleep(K_MSEC(500));
    iotex_hal_gpio_set(LED_RED, LED_OFF);
    k_sleep(K_MSEC(500));
    iotex_hal_gpio_set(LED_RED, LED_OFF);
    k_sleep(K_MSEC(500));
    sys_reboot(0);
}
