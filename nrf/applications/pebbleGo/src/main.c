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
#endif

#if defined(CONFIG_LWM2M_CARRIER)
#include <lwm2m_carrier.h>
#endif

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#include <dfu/mcuboot.h>
#endif

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

#if(CONFIG_IOTEX_BOARD_VERSION == 3)
Z_GENERIC_SECTION(.openocd_dbg.5) __attribute__((used)) const  uint8_t AppVersion[]= APP_VERSION_INFO;
#elif(CONFIG_IOTEX_BOARD_VERSION == 2)
Z_GENERIC_SECTION(.openocd_dbg.5) __attribute__((used)) const  uint8_t AppVersion[]=APP_VERSION_INFO;
#else
Z_GENERIC_SECTION(.openocd_dbg.5) __attribute__((used)) const  uint8_t AppVersion[]=APP_VERSION_INFO;
#endif
const uint8_t firmwareVersion[] = APP_VERSION;


#define   ENABLE_PSM_MODE       1
#define   DISABLE_PSM_MODE      (!(ENABLE_PSM_MODE))

//#if defined(CONFIG_BSD_LIBRARY)
//#include "nrf_inbuilt_key.h"
//#endif
#include <logging/log.h>
LOG_MODULE_REGISTER(asset_tracker, CONFIG_ASSET_TRACKER_LOG_LEVEL);

#if !defined(CONFIG_USE_PROVISIONED_CERTIFICATES)
#include "certificates.h"
#if defined(CONFIG_MODEM_KEY_MGMT)
#include <modem/modem_key_mgmt.h>
#endif

#endif
#define CALIBRATION_PRESS_DURATION  K_SECONDS(5)
#define CLOUD_CONNACK_WAIT_DURATION (CONFIG_CLOUD_WAIT_DURATION * MSEC_PER_SEC)

#ifdef CONFIG_ACCEL_USE_SIM
#define FLIP_INPUT			CONFIG_FLIP_INPUT
#define CALIBRATION_INPUT		-1
#else
#define FLIP_INPUT			-1
#ifdef CONFIG_ACCEL_CALIBRATE
#define CALIBRATION_INPUT		CONFIG_CALIBRATION_INPUT
#else
#define CALIBRATION_INPUT		-1
#endif /* CONFIG_ACCEL_CALIBRATE */
#endif /* CONFIG_ACCEL_USE_SIM */

#if defined(CONFIG_BSD_LIBRARY) && \
!defined(CONFIG_LTE_LINK_CONTROL)
#error "Missing CONFIG_LTE_LINK_CONTROL"
#endif

#if defined(CONFIG_BSD_LIBRARY) && \
defined(CONFIG_LTE_AUTO_INIT_AND_CONNECT) && \
defined(CONFIG_NRF_CLOUD_PROVISION_CERTIFICATES)
#error "PROVISION_CERTIFICATES \
	requires CONFIG_LTE_AUTO_INIT_AND_CONNECT to be disabled!"
#endif

#define SENSOR_PAYLOAD_MAX_LEN 150
#define RC_STR(rc) ((rc) == 0 ? "OK" : "ERROR")

#define PRINT_RESULT(func, rc) \
    printk("[%s:%d] %s: %d <%s>\n", __func__, __LINE__, \
           (func), rc, RC_STR(rc))
#define SUCCESS_OR_BREAK(rc) { if (rc != 0) { return ; } }


/* Stack definition for application workqueue */
K_THREAD_STACK_DEFINE(application_stack_area,
		      CONFIG_APPLICATION_WORKQUEUE_STACK_SIZE);
static struct k_work_q application_work_q;
static struct cloud_backend *cloud_backend;


/* File descriptor */
static struct pollfd fds;
/* MQTT Broker details. */
static struct mqtt_client client;

static bool do_reboot;

/* Forward declaration of functions */
static void periodic_publish_sensors_data(void);
static void sampling_and_store_sensor_data(void);
#if(!EXTERN_GPS)
static  void publish_gps_data(void);
#endif
static iotex_st_timestamp gps_timestamp;
/* Sensor data */
static struct gps_nmea gps_data;
double latitude;
double longitude;
static struct cloud_channel_data gps_cloud_data = {
	.type = CLOUD_CHANNEL_GPS,
	.tag = 0x1
};
static struct cloud_channel_data button_cloud_data;

#if CONFIG_MODEM_INFO
static struct modem_param_info modem_param;
static struct cloud_channel_data signal_strength_cloud_data;
#endif /* CONFIG_MODEM_INFO */

static s64_t gps_last_active_time;
static atomic_t carrier_requested_disconnect;
static atomic_t cloud_connect_attempts;

/* Flag used for flip detection */
static bool flip_mode_enabled = true;


#if IS_ENABLED(CONFIG_GPS_START_ON_MOTION)
/* Current state of activity monitor */
static motion_activity_state_t last_activity_state = MOTION_ACTIVITY_NOT_KNOWN;
#endif

/* Variable to keep track of nRF cloud association state. */
enum cloud_association_state {
	CLOUD_ASSOCIATION_STATE_INIT,
	CLOUD_ASSOCIATION_STATE_REQUESTED,
	CLOUD_ASSOCIATION_STATE_PAIRED,
	CLOUD_ASSOCIATION_STATE_RECONNECT,
	CLOUD_ASSOCIATION_STATE_READY,
};
static atomic_val_t cloud_association =
	ATOMIC_INIT(CLOUD_ASSOCIATION_STATE_INIT);

/* Structures for work */
//static struct k_work send_gps_data_work;
#if(!EXTERN_GPS)
static struct k_work send_gps_data_work;
#endif
static struct k_delayed_work send_env_data_work;
static struct k_delayed_work long_press_button_work;
static struct k_delayed_work   heartbeat_work;
static struct k_delayed_work   animation_work;

#if defined(CONFIG_AT_CMD)
#define MODEM_AT_CMD_BUFFER_LEN (CONFIG_AT_CMD_RESPONSE_MAX_LEN + 1)
#else
#define MODEM_AT_CMD_NOT_ENABLED_STR "Error: AT Command driver is not enabled"
#define MODEM_AT_CMD_BUFFER_LEN (sizeof(MODEM_AT_CMD_NOT_ENABLED_STR))
#endif
#define MODEM_AT_CMD_RESP_TOO_BIG_STR "Error: AT Command response is too large to be sent"
#define MODEM_AT_CMD_MAX_RESPONSE_LEN (2000)
static char modem_at_cmd_buff[MODEM_AT_CMD_BUFFER_LEN];
static K_SEM_DEFINE(modem_at_cmd_sem, 1, 1);
static K_SEM_DEFINE(cloud_disconnected, 0, 1);
#if defined(CONFIG_LWM2M_CARRIER)
static void app_disconnect(void);
static K_SEM_DEFINE(bsdlib_initialized, 0, 1);
static K_SEM_DEFINE(lte_connected, 0, 1);
static K_SEM_DEFINE(cloud_ready_to_connect, 0, 1);
#endif
static unsigned int mqttSentCount = 0;
#define   MQTT_NO_RESPONSE_MAX_COUNT   3

#if CONFIG_MODEM_INFO
static struct k_delayed_work rsrp_work;
#endif /* CONFIG_MODEM_INFO */

enum error_type {
	ERROR_CLOUD,
	ERROR_BSD_RECOVERABLE,
	ERROR_LTE_LC,
	ERROR_SYSTEM_FAULT
};

#if CONFIG_LIGHT_SENSOR
static void light_sensor_data_send(void);
#endif /* CONFIG_LIGHT_SENSOR */
static void sensors_init(void);
static void work_init(void);
static void sensor_data_send(struct cloud_channel_data *data);
static void device_status_send(struct k_work *work);
static void connection_evt_handler(const struct cloud_event *const evt);

/**@brief nRF Cloud error handler. */
void error_handler(enum error_type err_type, int err_code)
{
    if (err_type == ERROR_CLOUD) {
        if (mqtt_disconnect(&client)) {
            printk("Could not disconnect MQTT client during error handler.\n");
        }
#if(!EXTERN_GPS)
        if (gps_control_is_enabled()) {
            printk("Reboot\n");
            sys_reboot(0);
        }
#else
        //sys_reboot(0);        
        k_delayed_work_cancel(&send_env_data_work);
        return;
#endif

#if defined(CONFIG_LTE_LINK_CONTROL)
        /* Turn off and shutdown modem */
        printk("LTE link disconnect\n");
        int err = lte_lc_power_off();

        if (err) {
            printk("lte_lc_power_off failed: %d\n", err);
        }

#endif /* CONFIG_LTE_LINK_CONTROL */
#if defined(CONFIG_BSD_LIBRARY)
        printk("Shutdown modem\n");
        bsdlib_shutdown();
#endif
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
            printk("Error of type ERROR_CLOUD: %d\n", err_code);
            break;

        case ERROR_BSD_RECOVERABLE:
            /* Blinking all LEDs ON/OFF in pairs (1 and 3, 2 and 4)
             * if there is a recoverable error.
             */
            ui_led_set_pattern(UI_LED_ERROR_BSD_REC);
            printk("Error of type ERROR_BSD_RECOVERABLE: %d\n", err_code);
            break;

        case ERROR_BSD_IRRECOVERABLE:
            /* Blinking all LEDs ON/OFF if there is an
             * irrecoverable error.
             */
            ui_led_set_pattern(UI_LED_ERROR_BSD_IRREC);
            printk("Error of type ERROR_BSD_IRRECOVERABLE: %d\n", err_code);
            break;

        default:
            /* Blinking all LEDs ON/OFF in pairs (1 and 2, 3 and 4)
             * undefined error.
             */
            ui_led_set_pattern(UI_LED_ERROR_UNKNOWN);
            printk("Unknown error type: %d, code: %d\n",
                   err_type, err_code);
            break;
    }

    while (true) {       
        k_cpu_idle();
    }

#endif /* CONFIG_DEBUG */
}

static void send_agps_request(struct k_work *work)
{
	ARG_UNUSED(work);

#if defined(CONFIG_NRF_CLOUD_AGPS)
	int err;
	static s64_t last_request_timestamp;

/* Request A-GPS data no more often than every hour (time in milliseconds). */
#define AGPS_UPDATE_PERIOD (60 * 60 * 1000)

	if ((last_request_timestamp != 0) &&
	    (k_uptime_get() - last_request_timestamp) < AGPS_UPDATE_PERIOD) {
		LOG_WRN("A-GPS request was sent less than 1 hour ago");
		return;
	}

	LOG_INF("Sending A-GPS request");

	err = nrf_cloud_agps_request_all();
	if (err) {
		LOG_ERR("A-GPS request failed, error: %d", err);
		return;
	}

	last_request_timestamp = k_uptime_get();

	LOG_INF("A-GPS request sent");
#endif /* defined(CONFIG_NRF_CLOUD_AGPS) */
}

/**@brief Recoverable BSD library error. */
void bsd_recoverable_error_handler(uint32_t err)
{
	error_handler(ERROR_BSD_RECOVERABLE, (int)err);
}

#if(!EXTERN_GPS)
static void send_gps_data_work_fn(struct k_work *work)
{
	//sensor_data_send(&gps_cloud_data);
	publish_gps_data();
}
#endif
static void send_env_data_work_fn(struct k_work *work) {
#if(!EXTERN_GPS)    
    printk("[%s:%d, %d]\n", __func__, __LINE__, gps_control_is_active());
#endif
    if (!atomic_get(&send_data_enable)) {
        return;
    }
#if(!EXTERN_GPS)
    if (gps_control_is_active()) {
        k_delayed_work_submit(&send_env_data_work,
                              K_SECONDS(CONFIG_ENVIRONMENT_DATA_BACKOFF_TIME));
        return;
    }
#endif
    config_mutex_lock();
    if (iotex_mqtt_is_bulk_upload()) {
        sampling_and_store_sensor_data();
        //k_delayed_work_submit(&send_env_data_work, K_SECONDS(iotex_mqtt_get_sampling_frequency()));
        k_delayed_work_submit_to_queue(&application_work_q, &send_env_data_work, K_SECONDS(iotex_mqtt_get_sampling_frequency())); 
    }
    else {
        periodic_publish_sensors_data();
        //k_delayed_work_submit_to_queue(&application_work_q, &send_env_data_work, K_SECONDS(iotex_mqtt_get_upload_period()));
        k_delayed_work_submit_to_queue(&application_work_q, &send_env_data_work, K_SECONDS(30));
    }
    config_mutex_unlock();
    return;
}

static void uploadSensorData(void) {
    if (!atomic_get(&send_data_enable)) {
        return;
    }
    config_mutex_lock();
    if (iotex_mqtt_is_bulk_upload()) {
        sampling_and_store_sensor_data();
    }
    else {
        periodic_publish_sensors_data();
    }
    pubOnePack();
    config_mutex_unlock();   
}

void RestartEnvWork(int s)
{
    k_delayed_work_cancel(&send_env_data_work);
    k_delayed_work_submit(&send_env_data_work, K_SECONDS(s));
}

#if(!EXTERN_GPS)
static void gps_handler(struct device *dev, struct gps_event *evt)
{
	gps_last_active_time = k_uptime_get();

	switch (evt->type) {
	case GPS_EVT_SEARCH_STARTED:
		LOG_INF("GPS_EVT_SEARCH_STARTED");
		gps_control_set_active(true);
		ui_led_set_pattern(UI_LED_GPS_SEARCHING);
		break;
	case GPS_EVT_SEARCH_STOPPED:
		LOG_INF("GPS_EVT_SEARCH_STOPPED");
		gps_control_set_active(false);
		ui_led_set_pattern(UI_CLOUD_CONNECTED);
		break;
	case GPS_EVT_SEARCH_TIMEOUT:
		LOG_INF("GPS_EVT_SEARCH_TIMEOUT");
		gps_control_set_active(false);
		LOG_INF("GPS will be attempted again in %d seconds",
			gps_control_get_gps_reporting_interval());
		break;
	case GPS_EVT_PVT:
		/* Don't spam logs */
		break;
	case GPS_EVT_PVT_FIX:
		LOG_INF("GPS_EVT_PVT_FIX");
		break;
	case GPS_EVT_NMEA:
		/* Don't spam logs */
		break;
	case GPS_EVT_NMEA_FIX:
		LOG_INF("Position fix with NMEA data");

		memcpy(gps_data.buf, evt->nmea.buf, evt->nmea.len);
		gps_data.len = evt->nmea.len;
		iotex_modem_get_clock(&gps_timestamp);
		gps_cloud_data.data.buf = gps_data.buf;
		gps_cloud_data.data.len = gps_data.len;
		gps_cloud_data.tag += 1;

		if (gps_cloud_data.tag == 0) {
			gps_cloud_data.tag = 0x1;
		}
		latitude = evt->pvt.latitude;
		longitude = evt->pvt.longitude;

		ui_led_set_pattern(UI_LED_GPS_FIX);
		gps_control_set_active(false);
		LOG_INF("GPS will be started in %d seconds",
			gps_control_get_gps_reporting_interval());

		k_work_submit_to_queue(&application_work_q,
				       &send_gps_data_work);
		//env_sensors_poll();
		break;
	case GPS_EVT_OPERATION_BLOCKED:
		LOG_INF("GPS_EVT_OPERATION_BLOCKED");
		ui_led_set_pattern(UI_LED_GPS_BLOCKED);
		break;
	case GPS_EVT_OPERATION_UNBLOCKED:
		LOG_INF("GPS_EVT_OPERATION_UNBLOCKED");
		ui_led_set_pattern(UI_LED_GPS_SEARCHING);
		break;
	case GPS_EVT_AGPS_DATA_NEEDED:
		LOG_INF("GPS_EVT_AGPS_DATA_NEEDED");
		/* Send A-GPS request with short delay to avoid LTE network-
		 * dependent corner-case where the request would not be sent.
		 */
		k_delayed_work_submit_to_queue(&application_work_q,
					       &send_agps_request_work,
					       K_SECONDS(1));
		break;
	case GPS_EVT_ERROR:
		LOG_INF("GPS_EVT_ERROR\n");
		break;
	default:
		break;
	}
}
#endif

#if 0
//#if !defined(CONFIG_USE_PROVISIONED_CERTIFICATES)

#warning Not for prodcution use. This should only be used once to provisioning the certificates please deselect the provision certificates configuration and compile again.
#define MAX_OF_2 MAX(sizeof(NRF_CLOUD_CA_CERTIFICATE),\
		     sizeof(NRF_CLOUD_CLIENT_PRIVATE_KEY))
#define MAX_LEN MAX(MAX_OF_2, sizeof(NRF_CLOUD_CLIENT_PUBLIC_CERTIFICATE))
static u8_t certificates[][MAX_LEN] = {{NRF_CLOUD_CA_CERTIFICATE},
				       {NRF_CLOUD_CLIENT_PRIVATE_KEY},
				       {NRF_CLOUD_CLIENT_PUBLIC_CERTIFICATE} };
static const size_t cert_len[] = {
	sizeof(NRF_CLOUD_CA_CERTIFICATE) - 1, sizeof(NRF_CLOUD_CLIENT_PRIVATE_KEY) - 1,
	sizeof(NRF_CLOUD_CLIENT_PUBLIC_CERTIFICATE) - 1
};
static int provision_certificates(void)
{
	int err;
	sec_tag_t sec_tag = CONFIG_CLOUD_CERT_SEC_TAG;
	enum modem_key_mgnt_cred_type cred[] = {
		MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
		MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT,
		MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT,
	};

	/* Delete certificates */
	for (enum modem_key_mgnt_cred_type type = 0; type < 3; type++) {
		err = modem_key_mgmt_delete(sec_tag, type);
		printk("modem_key_mgmt_delete(%u, %d) => result=%d\n",
				sec_tag, type, err);
	}

	/* Write certificates */
	for (enum modem_key_mgnt_cred_type type = 0; type < 3; type++) {
		err = modem_key_mgmt_write(sec_tag, cred[type],
				certificates[type], cert_len[type]);
		printk("modem_key_mgmt_write => result=%d\n", err);
	}
	return 0;
}
#endif

void WriteCertIntoModem(uint8_t *cert, uint8_t *key, uint8_t *root )
{
    u8_t *certificates[]={root, key, cert};
    size_t cert_len[] = { strlen(root),strlen(key),strlen(cert)};
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
		printk("modem_key_mgmt_delete(%u, %d) => result=%d\n",
				sec_tag, type, err);
	}

	/* Write certificates */
	for (enum modem_key_mgnt_cred_type type = 0; type < 3; type++) {
		err = modem_key_mgmt_write(sec_tag, cred[type],
				certificates[type], cert_len[type]);
		printk("modem_key_mgmt_write => result=%d\n", err);                        
	}
	return;    
}

#if(!EXTERN_GPS)
static char *get_mqtt_payload_gps(enum mqtt_qos qos) {
    static char payload[SENSOR_PAYLOAD_MAX_LEN];

    printf("{GPS latitude:%f, longitude:%f, timestamp %s\n", latitude, longitude, gps_timestamp.data);
    snprintf(payload, sizeof(payload), "{\"Device\":\"%s\",\"latitude\":%f,\"longitude\":%f, \"timestamp\":%s}",
             "GPS", latitude, longitude, gps_timestamp.data);
    return payload;
}

void publish_gps_data() {
    int rc;
    if (iotex_mqtt_is_connected()) {
        SUCCESS_OR_BREAK(mqtt_ping(&client));
        rc = iotex_mqtt_publish_data(&client, 0, get_mqtt_payload_gps(0));
        PRINT_RESULT("mqtt_publish_gps", rc);
    }
}
#endif

#include <drivers/pwm.h>
static int iotex_buzzer_test(void)
{
	const char *dev_name = "PWM_1";
	int err = 0;
    struct device *pwm_dev;

	pwm_dev = device_get_binding(dev_name);
	if (!pwm_dev) {
		printk("Could not bind to device %s", dev_name);
		return -ENODEV;
	}
    
    // 2.7kHz ==370us  185/370=50% duty
    pwm_pin_set_usec(pwm_dev, 11,370, 185, 0);
	k_sleep(K_MSEC(500));
    pwm_pin_set_usec(pwm_dev, 11, 0, 0, 0);
	return 0;
}

// check if mqtt session is connected
static bool ismqttOffline(void)
{
    return mqttSentCount >= MQTT_NO_RESPONSE_MAX_COUNT;
}

static void mqttSentOnce(void)
{
    mqttSentCount++;
}

void mqttGetResponse(void)
{
    mqttSentCount = 0;
}

static void periodic_publish_sensors_data(void) {
    int rc;
    char *pbuf = NULL;

    if (iotex_mqtt_is_connected() && !ismqttOffline()) {
        SUCCESS_OR_BREAK(mqtt_ping(&client));
        pbuf = (char *)malloc(DATA_BUFFER_SIZE);
        if (!pbuf) {
            printk("malloc buffer failed\n");
            return;
        }
        if (rc = SensorPackage(iotex_mqtt_get_data_channel(), pbuf)) {
            rc = iotex_mqtt_publish_data(&client, 0, pbuf, rc);
            printk("mqtt_publish_devicedata: %d \n", rc);
        } else {
            printk("mqtt package error ! \n");
        }
        free(pbuf);
    } else {
        printk("periodic_publish_sensors_data mqttSentCount : %d \n", mqttSentCount);
        sys_reboot(0);
    }
}

int publish_dev_ownership(char *buf, int len)
{
    return iotex_mqtt_publish_ownership(&client, 0, buf, len);
}

int publish_dev_query(char *buf, int len)
{
    return iotex_mqtt_publish_query(&client, 0, buf, len);
}

#if IS_ENABLED(CONFIG_GPS_START_ON_MOTION)
static bool motion_activity_is_active(void)
{
	return (last_activity_state == MOTION_ACTIVITY_ACTIVE);
}

static void motion_trigger_gps(motion_data_t motion_data)
{
	static bool initial_run = true;

	/* The handler is triggered once on startup, regardless of motion,
	 * in order to get into a known state. This should be ignored.
	 */
	if (initial_run) {
		initial_run = false;
		return;
	}

	if (motion_activity_is_active() && gps_control_is_enabled()) {
		static s64_t next_active_time = 0;
		s64_t last_active_time = gps_last_active_time / 1000;
		s64_t now = k_uptime_get() / 1000;
		s64_t time_since_fix_attempt = now - last_active_time;
		s64_t time_until_next_attempt = next_active_time - now;

		LOG_DBG("Last at %lld s, now %lld s, next %lld s; "
			"%lld secs since last, %lld secs until next",
			last_active_time, now, next_active_time,
			time_since_fix_attempt, time_until_next_attempt);

		if (time_until_next_attempt >= 0) {
			LOG_DBG("keeping original schedule.");
			return;
		}

		time_since_fix_attempt = MAX(0, (MIN(time_since_fix_attempt,
			CONFIG_GPS_CONTROL_FIX_CHECK_INTERVAL)));
		s64_t time_to_start_next_fix = 1 +
			CONFIG_GPS_CONTROL_FIX_CHECK_INTERVAL -
			time_since_fix_attempt;

		next_active_time = now + time_to_start_next_fix;

		char buf[100];

		/* due to a known design issue in Zephyr, we need to use
		 * snprintf to output floats; see:
		 * https://github.com/zephyrproject-rtos/zephyr/issues/18351
		 * https://github.com/zephyrproject-rtos/zephyr/pull/18921
		 */
		snprintf(buf, sizeof(buf),
			"Motion triggering GPS; accel, %.1f, %.1f, %.1f",
			motion_data.acceleration.x,
			motion_data.acceleration.y,
			motion_data.acceleration.z);
		LOG_INF("%s", log_strdup(buf));

		LOG_INF("starting GPS in %lld seconds", time_to_start_next_fix);
		gps_control_start(time_to_start_next_fix * MSEC_PER_SEC);
	}
}
#endif

static void long_press_handler(struct k_work *work) {
    if (!atomic_get(&send_data_enable)) {
        printk("Link not ready, long press disregarded\n");
        return;
    }
#if(!EXTERN_GPS)
    if (gps_control_is_enabled()) {
        printk("Stopping GPS\n");
        gps_control_disable();
    } else {
        printk("Starting GPS\n");
        gps_control_enable();
        gps_control_start(1000);
    }
#endif    
}

void heartbeat_work_fn(struct k_work *work) 
{
    iotex_mqtt_heart_beat(&client, 0);
    k_delayed_work_submit_to_queue(&application_work_q, &heartbeat_work, K_SECONDS(20)); 
}

void animation_work_fn(struct k_work *work)
{      
    sta_Refresh();
    k_delayed_work_submit_to_queue(&application_work_q, &animation_work, K_SECONDS(1));
}

//static void power_off_handler(struct k_work *work)
//{    
//    gpio_poweroff();
//}

/**@brief Initializes and submits delayed work. */
static void work_init(void)
{
#if(!EXTERN_GPS)     
	k_work_init(&send_gps_data_work, send_gps_data_work_fn);
#endif
	//k_delayed_work_init(&send_env_data_work, send_env_data_work_fn);
	//k_delayed_work_init(&send_agps_request_work, send_agps_request);
	k_delayed_work_init(&long_press_button_work, long_press_handler);
	//k_delayed_work_init(&power_off_button_work, power_off_handler);
    //k_delayed_work_init(&heartbeat_work, heartbeat_work_fn);
    k_delayed_work_init(&animation_work, animation_work_fn);
}

/**@brief Configures modem to provide LTE link. Blocks until link is
 * successfully established.
 */
static void modem_configure(void)
{
#if defined(CONFIG_BSD_LIBRARY)

    if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
        /* Do nothing, modem is already turned on
         * and connected.
         */
    } else {
        int err;

        printk("Connecting to LTE network. ");
        printk("This may take several minutes.\n");  
        err = lte_lc_psm_req(true);
        if (err) {
            printk("lte_lc_psm_req erro:%d\n", err);
            error_handler(ERROR_LTE_LC, err);
        }              
        //ui_led_set_pattern(UI_LTE_CONNECTING);
        //ui_led_deactive(LTE_CONNECT_MASK,1);
        err = lte_lc_init_and_connect();

        if (err) {
            printk("LTE link could not be established.\n");
            error_handler(ERROR_LTE_LC, err);
        }

        printk("Connected to LTE network\n");
        //ui_led_active(LTE_CONNECT_MASK,1);
        sta_SetMeta(LTE_LINKER, STA_LINKER_ON);
    }

#endif
}

/**@brief Initializes the sensors that are used by the application. */
static void sensors_init(void)
{
#if(EXTERN_GPS)
    //exGPSInit();
#else
    int err;
    //gps_control_init(gps_trigger_handler);
	err = gps_control_init(&application_work_q, gps_handler);
	if (err) {
		LOG_ERR("GPS could not be initialized");
		return;
	}
#endif      
    /* Send sensor data after initialization, as it may be a long time until
     * next time if the application is in power optimized mode.
     */
    k_delayed_work_submit(&send_env_data_work, K_SECONDS(10));
  
    printk("[%s:%d] Sensors initialized\n", __func__, __LINE__);
}

#if defined(CONFIG_USE_UI_MODULE)
/**@brief User interface event handler. */
static void ui_evt_handler(struct ui_evt evt)
{
    printk("ui evt handler %d\n", evt.button);

    if (IS_ENABLED(CONFIG_GPS_CONTROL_ON_LONG_PRESS) &&
       (evt.button == UI_BUTTON_1)) {
        if (IsDevReg()) {
            if (devRegGet() == 1) {
                devRegSet(DEV_REG_SIGN_SEND);
                // SetIndicator(UI_WAIT_ACK);
                hintString(htRegWaitACK,HINT_TIME_FOREVER);
            }
        } else {
            if (evt.type == UI_EVT_BUTTON_ACTIVE) {
                k_delayed_work_submit(&long_press_button_work,
                K_SECONDS(2));// chang 5S to 2S
                // k_delayed_work_submit(&power_off_button_work,K_SECONDS(5));
            } else {
                k_delayed_work_cancel(&long_press_button_work);
                // k_delayed_work_cancel(&power_off_button_work);
            }
        }
    }

#if defined(CONFIG_LTE_LINK_CONTROL)

    if ((evt.button == UI_SWITCH_2) &&
        IS_ENABLED(CONFIG_POWER_OPTIMIZATION_ENABLE)) {
        int err;

        if (evt.type == UI_EVT_BUTTON_ACTIVE) {
            err = lte_lc_edrx_req(false);

            if (err) {
                error_handler(ERROR_LTE_LC, err);
            }

            err = lte_lc_psm_req(true);

            if (err) {
                error_handler(ERROR_LTE_LC, err);
            }
        } else {
            err = lte_lc_psm_req(false);

            if (err) {
                error_handler(ERROR_LTE_LC, err);
            }

            err = lte_lc_edrx_req(true);

            if (err) {
                error_handler(ERROR_LTE_LC, err);
            }
        }
    }

#endif /* defined(CONFIG_LTE_LINK_CONTROL) */
}
#endif /* defined(CONFIG_USE_UI_MODULE) */

void handle_bsdlib_init_ret(void)
{
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

void iotex_mqtt_bulk_upload_sampling_data(uint16_t channel) {
    int rc;
    struct mqtt_payload msg;
	uint8_t buffer[128];
    // TODO: should be removed
}

static void sampling_and_store_sensor_data(void) {
    /* Data sampling mode */ 
    if (iotex_mqtt_is_need_sampling()) {
        /* Sampling data and save to nvs,
           when required sampling count fulfilled, start bulk upload.
           Support breakpoint resampling and breakpoint retransmission.
        */

        printk("Before...............\n");

        if (!iotex_mqtt_sampling_data_and_store(iotex_mqtt_get_data_channel())) {
            printk("[%s:%d] Sampling and store data failed!\n", __func__, __LINE__);
            return;
        }

#ifdef CONFIG_DEBUG_MQTT_CONFIG
        printk("[%s:%d]: Sampling count: %u\n", __func__, __LINE__, iotex_mqtt_get_current_sampling_count() + 1);
#endif

        /* Required sampling count is fulfilled */
        if (iotex_mqtt_inc_current_sampling_count()) {
            //iotex_mqtt_bulk_upload_sampling_data(iotex_mqtt_get_data_channel());
        }
    }
    /* Data upload mode */
    else {
        iotex_mqtt_bulk_upload_sampling_data(iotex_mqtt_get_data_channel());
    }
}

const uint8_t test_string[]="{\"message\":{\"deviceAvatar\":\"w232342342432\",\"deviceName\":\"777777777\",\"imei\":\"352656103381102\",\"walletAddress\":\"io1zgdezueqq2ekjlp0v3ur7l5r7djuh6yrsuzsws\"}}";

void sendHearBeat(void)
{
    //k_delayed_work_submit_to_queue(&application_work_q, &heartbeat_work, K_NO_WAIT);
    iotex_mqtt_heart_beat(&client, 0);
}

void stopHeartBeat(void)
{
    k_delayed_work_cancel(&heartbeat_work);
}

void stopMqtt(void)
{
    mqtt_disconnect(&client);
}

void StartSendEnvData(uint32_t sec)
{
    printk("start send env data!\n");
    k_delayed_work_submit_to_queue(&application_work_q, &send_env_data_work, K_SECONDS(sec));
}

extern void sta_Refresh(void);

void data_test(void)
{
    char *pbuf = NULL;
    int rc;
    printk("[%s:%d]\n", __func__, __LINE__);
    pbuf = (char *)malloc(DATA_BUFFER_SIZE);
    if (!pbuf) {
        printk("malloc failed\n");
        return;
    }

    /* Upload selected channel data */
    if (rc = SensorPackage(iotex_mqtt_get_data_channel(), pbuf)) {
        printk("mqtt_publish_devicedata: %d \n", rc);
    } else {
        printk("mqtt package error ! \n");
    }
    free(pbuf);
}

void main(void)
{
    int err, errCounts = 0;

	LOG_INF("Asset tracker started");
	k_work_q_start(&application_work_q, application_stack_area,
		       K_THREAD_STACK_SIZEOF(application_stack_area),
		       CONFIG_APPLICATION_WORKQUEUE_PRIORITY);
	if (IS_ENABLED(CONFIG_WATCHDOG)) {
		watchdog_init_and_start(&application_work_q);
	}
    //  init ECDSA
    InitLowsCalc();
    if (initECDSA_sep256r()) {
        printk("initECDSA_sep256r error\n");
        return;
    }

	/* HAL init, notice gpio must be the first (to set IO_POWER_ON on )*/
    iotex_local_storage_init();

    if (startup_check_ecc_key()) {
        printk("check ecc key error\n");
        printk("system will not startup\n");
        return;
    }

    iotex_hal_gpio_init();
    iotex_hal_adc_init();
    /* Iotex Init BME680 */
    iotex_bme680_init();
    /* Iotex Init TSL2572 */
    iotex_TSL2572_init(GAIN_1X); 
    /* Iotex Init ICM42605 */
    iotex_icm42605_init();	
	iotex_key_init();
    //updateLedPattern();
	work_init();

    ssd1306_init();

    MainMenu();
    appEntryDetect();

    k_delayed_work_submit_to_queue(&application_work_q, &animation_work, K_MSEC(100));
	modem_configure();

#if defined(CONFIG_LWM2M_CARRIER)
	k_sem_take(&bsdlib_initialized, K_FOREVER);
#else
	handle_bsdlib_init_ret();
#endif
#ifdef CONFIG_UNITTEST
    unittest();
#endif
    exGPSInit(); 
    initOTA();
    sta_Refresh();        

    while(true){
        if ((err = iotex_mqtt_client_init(&client, &fds))) {
            errCounts++;
            if(errCounts < 3)
                continue;
            printk("ERROR: mqtt_connect %d, rebooting...\n", err);
            k_sleep(K_MSEC(500));
            sys_reboot(0);
            return;
        }
        //printf("GPS latitude:%lf, longitude:%lf\n", latitude, longitude);
        errCounts = 0;      
        if(!iotexDevBinding(&fds,&client)) {
            printk("upload sensor data\n");            
            uploadSensorData();
            k_sleep(K_MSEC(200));
            mqtt_disconnect(&client); 
            gpsPowerOff();
            printk("start sleep\n");  
            setModemSleep(1);
            lte_lc_psm_req(true);                       
            k_sleep(K_SECONDS(270));
            gpsPowerOn();
            k_sleep(K_SECONDS(30));
            lte_lc_psm_req(false);  
            printk("wake up\n"); 
            setModemSleep(0);
        }
        //k_delayed_work_cancel(&send_env_data_work);
        if(devRegGet() != DEV_REG_STOP) {
            devRegSet(DEV_REG_START);
            ssd1306_display_logo();
        }        
    }
#if defined(CONFIG_LWM2M_CARRIER)
	LOG_INF("Waiting for LWM2M carrier to complete initialization...");
	k_sem_take(&cloud_ready_to_connect, K_FOREVER);
#endif
	//connect_to_cloud(0);
    printk("Disconnecting MQTT client...\n");
    err = mqtt_disconnect(&client);

    if (err) {
        printk("Could not disconnect MQTT client. Error: %d\n", err);
    }

    iotex_hal_gpio_set(LED_RED, LED_ON);
    k_sleep(K_MSEC(500));
    iotex_hal_gpio_set(LED_RED, LED_OFF);
    k_sleep(K_MSEC(500));
    iotex_hal_gpio_set(LED_RED, LED_OFF);
    k_sleep(K_MSEC(500));
    sys_reboot(0);
}
