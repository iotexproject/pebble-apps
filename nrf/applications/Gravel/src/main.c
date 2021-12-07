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
#if !defined(CONFIG_USE_PROVISIONED_CERTIFICATES)
#include "certificates.h"
#if defined(CONFIG_MODEM_KEY_MGMT)
#include <modem/modem_key_mgmt.h>
#endif /* CONFIG_MODEM_KEY_MGMT */
#endif /* CONFIG_USE_PROVISIONED_CERTIFICATES */

LOG_MODULE_REGISTER(main, CONFIG_ASSET_TRACKER_LOG_LEVEL);

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
#if !defined(CONFIG_DEBUG) && defined(CONFIG_REBOOT)
    LOG_PANIC();
    sys_reboot(0);
#else
    switch (err_type) {
        case ERROR_CLOUD:
            /* Blinking all LEDs ON/OFF in pairs (1 and 4, 2 and 3)
             * if there is an application error.
             */
            ui_led_set_pattern(UI_LED_ERROR_CLOUD);
            LOG_ERR("Error of type ERROR_CLOUD: %d\n", err_code);
            break;

        case ERROR_BSD_RECOVERABLE:
            /* Blinking all LEDs ON/OFF in pairs (1 and 3, 2 and 4)
             * if there is a recoverable error.
             */
            ui_led_set_pattern(UI_LED_ERROR_BSD_REC);
            LOG_ERR("Error of type ERROR_BSD_RECOVERABLE: %d\n", err_code);
            break;

        case ERROR_BSD_IRRECOVERABLE:
            /* Blinking all LEDs ON/OFF if there is an
             * irrecoverable error.
             */
            ui_led_set_pattern(UI_LED_ERROR_BSD_IRREC);
            LOG_ERR("Error of type ERROR_BSD_IRRECOVERABLE: %d\n", err_code);
            break;

        default:
            /* Blinking all LEDs ON/OFF in pairs (1 and 2, 3 and 4)
             * undefined error.
             */
            ui_led_set_pattern(UI_LED_ERROR_UNKNOWN);
            LOG_ERR("Unknown error type: %d, code: %d\n", err_type, err_code);
            break;
    }
#endif /* CONFIG_DEBUG */
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

/*  dev registration */
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
            LOG_ERR("LTE link could not be established.\n");
            error_handler(ERROR_LTE_LC, err);
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
        /* Required sampling count is fulfilled */
        iotex_mqtt_inc_current_sampling_count();
    }
}

/*  close mqtt net */
void stopMqtt(void) {
    mqtt_disconnect(&client);
}

void main(void) {
    int err, errCounts = 0;

    LOG_INF("APP %s  %s started", APP_NAME, RELEASE_VERSION);
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
    initOTA();
    /*  status bar refresh */
    sta_Refresh();

    while (true) {
        if ((err = iotex_mqtt_client_init(&client, &fds))) {
            errCounts++;
            if(errCounts < 4){
                LOG_INF("**** %s reconnection ****\n",reconnectReminder[errCounts]);
                continue;
            }
            LOG_ERR("ERROR: mqtt_connect %d, rebooting...\n", err);
            k_sleep(K_MSEC(500));
            sys_reboot(0);
            break;
        }

        errCounts = 0;
        if (!iotexDevBinding(&fds,&client)) {
            LOG_INF("upload sensor data\n");
            uploadSensorData();
            k_sleep(K_MSEC(200));
            mqtt_disconnect(&client);
            gpsPowerOff();
            LOG_INF("start sleep\n");
            setModemSleep(1);
            k_sleep(K_SECONDS(1));
            lte_lc_psm_req(true);
            k_sleep(K_SECONDS(SENSOR_UPLOAD_PERIOD-61));
            gpsPowerOn();
            k_sleep(K_SECONDS(60));
            lte_lc_psm_req(false);
            LOG_INF("wake up\n");
            setModemSleep(0);
        }

        if (devRegGet() != DEV_REG_STOP) {
            devRegSet(DEV_REG_START);
            ssd1306_display_logo();
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
