/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <sys/util.h>
#include <drivers/gps.h>
#include <modem/lte_lc.h>
#include <drivers/gpio.h>
#include <drivers/uart.h>
#include <stdlib.h>
#include <sys/mutex.h>


#include "ui.h"
#include "gps_controller.h"
#include "display.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(gps_control, CONFIG_ASSET_TRACKER_LOG_LEVEL);

// gps Mosaic ?
#define  USE_GPS_MOSAIC	 1


#define  GPIO_DIR_OUT  GPIO_OUTPUT
#define  gpio_pin_write gpio_pin_set

#define  CHECK_POINT(a, b, c) \
        a = b;             \
		if(a == NULL)      \
			return 0;        \
		else{             \
			a++;          \
			if(a == c) return 0; \
		}

// board v2.0  extern gps
#if(EXTERN_GPS)
static  struct device *guart_dev_gps;
struct device *gpsPower;
static u8_t uart_buf1[128];
static u8_t pos1=0;
static uint8_t  bGpsHead = 0;
const char strGRMC[7]="$GNRMC";


struct gprmc {
    double latitude;
    char lat;
    double longitude;
    char lon;
    double speed;
    double course;
};
typedef struct gprmc gprmc_t;

struct sys_mutex iotex_gps_mutex;
static struct k_work   gpsPackageParse;
static  double gpsLat = 200.0,gpsLon = 200.0;
static  uint8_t bgpsAtive = 0;	

static int getRMC(char *nmea, gprmc_t *loc);

void gpsPackageParseHandle(struct k_work *work){
	gprmc_t rmc;
    int intpart;
    double fractpart; 
	double lat,lon;	
	if(!getRMC(uart_buf1,&rmc)){		
		// lat 
		intpart= rmc.latitude/100;
		fractpart=((rmc.latitude/100)-intpart)/0.6;
		lat = intpart+fractpart;
		// lon
		intpart= rmc.longitude/100;
		fractpart=((rmc.longitude/100)-intpart)/0.6; 
		lon = intpart+fractpart;
#if USE_GPS_MOSAIC		
		// gps  Mosaic needed ?
		fractpart = lat * 100;
		intpart = fractpart / 1;
		fractpart -= intpart;
		fractpart /= 100.0;
		lat -= fractpart;
		// 
		fractpart = lon * 100;
		intpart = fractpart / 1;
		fractpart -= intpart;
		fractpart /= 100.0;
		lon -= fractpart;	
#endif		
		// South West ?
		if(rmc.lat == 'S')                     
			lat *= (-1);  
		if(rmc.lon == 'W')                     
			lon *= (-1); 
		sys_mutex_lock(&iotex_gps_mutex, K_FOREVER);
		gpsLat = lat;
		gpsLon = lon;		
		bgpsAtive = 1;
		sys_mutex_unlock(&iotex_gps_mutex); 
		sta_SetMeta(GPS_LINKER, STA_LINKER_ON);		
	} 
	else {
		sys_mutex_lock(&iotex_gps_mutex, K_FOREVER);
		gpsLat = 200.0;
		gpsLon = 200.0;
		bgpsAtive = 0;
		sys_mutex_unlock(&iotex_gps_mutex); 
		sta_SetMeta(GPS_LINKER, STA_LINKER_OFF);				
	}
	uart_irq_rx_enable(guart_dev_gps);
}


static void uart_gps_rx_handler(u8_t character) {
	//printk("%c", character);
	if(!bGpsHead){
		if( 0x24 == character) {
			pos1=1;
			bGpsHead = 1;
			uart_buf1[0] = character;
		}
	}
	else {
		uart_buf1[pos1] = character;
		if((uart_buf1[pos1 - 1] == '\r') && (character == '\n')) {
			uart_buf1[pos1 - 1] ='\n'; //delete '\r',printk  '\n' to '\r'+'\n'
			uart_buf1[pos1] =0;				   
			if(strncmp(uart_buf1,strGRMC,strlen(strGRMC))==0) {
				uart_irq_rx_disable(guart_dev_gps);
                //printk("%s",uart_buf1);
				k_work_submit(&gpsPackageParse);				
			}
			pos1=0;
			bGpsHead = 0;
		}
		else {
			pos1++;
			if(pos1 > (sizeof(uart_buf1)-1)){
				pos1 = 0;
				bGpsHead = 0;	
			}
			else {
				if(pos1 == strlen(strGRMC)) {
					if(strncmp(uart_buf1,strGRMC,strlen(strGRMC))) {
						pos1=0;
						bGpsHead = 0;						
					}
				}
			}		
		}		
	}
}

static void uart_gps_cb(struct device *dev)
{
	u8_t character;

	uart_irq_update(dev);

	if (!uart_irq_rx_ready(dev)) {
		return;
	}

	while (uart_fifo_read(dev, &character, 1)) {
		uart_gps_rx_handler(character);
	}
}

static int getRMC(char *nmea, gprmc_t *loc)
{
    //GNRMC,084852.000,A,2236.9453,N,11408.4790,E,0.53,292.44,141216,,,A*75
    char *p = nmea;
	char *end = nmea + strlen(nmea);
	char buf[20]={0};
    CHECK_POINT(p, strchr(p, ','), end);
    CHECK_POINT(p, strchr(p, ','), end);
	if(p[0] != 'A')
		return -1;
    CHECK_POINT(p, strchr(p, ','), end);
	memcpy(buf,p, strchr(p, ',')-p);
	if(strlen(buf) == 0)
		return -1;
    loc->latitude = atof(buf);
    CHECK_POINT(p, strchr(p, ','), end);
    switch (p[0]) {
    case 'N':
        loc->lat = 'N';
        break;
    case 'S':
        loc->lat = 'S';
        break;
    case ',':
        loc->lat = '\0';
        break;
    }

    CHECK_POINT(p, strchr(p, ','), end);
	memset(buf,0,sizeof(buf));
	memcpy(buf,p, strchr(p, ',')-p);
    loc->longitude = atof(buf);

    CHECK_POINT(p, strchr(p, ','), end);
    switch(p[0]) {
    case 'W':
        loc->lon = 'W';
        break;
    case 'E':
        loc->lon = 'E';
        break;
    case ',':
        loc->lon = '\0';
        break;
    }

    CHECK_POINT(p, strchr(p, ','), end);
    loc->speed = atof(p);

    CHECK_POINT(p, strchr(p, ','), end);
    loc->course = atof(p);

	return 0;
}

int getGPS(double *lat, double *lon)
{
	int ret = -1;
	sys_mutex_lock(&iotex_gps_mutex, K_FOREVER);
	if(bgpsAtive) {
		*lat = gpsLat;
		*lon = gpsLon;
		ret = 0;
	}
	sys_mutex_unlock(&iotex_gps_mutex); 	
	return ret;	
}

void exGPSStart(void)
{
	uart_irq_rx_enable(guart_dev_gps);
}

void exGPSStop(void)
{
	uart_irq_rx_disable(guart_dev_gps);
}

void exGPSInit(void) {		
	k_work_init(&gpsPackageParse, gpsPackageParseHandle);
	sys_mutex_init(&iotex_gps_mutex);	
	gpsPower = device_get_binding("GPIO_0");
	gpio_pin_configure(gpsPower, GPS_EN, GPIO_DIR_OUT); 
	gpio_pin_write(gpsPower, GPS_EN, 1);	
	guart_dev_gps=device_get_binding(UART_GPS);

	uart_irq_callback_set(guart_dev_gps, uart_gps_cb);
	gpio_pin_write(gpsPower, GPS_EN, 0);	
	uart_irq_rx_enable(guart_dev_gps);
	k_sleep(K_MSEC(10));
	gpio_pin_write(gpsPower, GPS_EN, 1);
}

void gpsPowerOn(void) {
	//pos1=0;
	//bGpsHead = 0;	
	//uart_irq_rx_enable(guart_dev_gps);	
	gpio_pin_write(gpsPower, GPS_EN, 1);
}
void gpsPowerOff(void) {	
	gpio_pin_write(gpsPower, GPS_EN, 0);
	//uart_irq_rx_disable(guart_dev_gps);
	//sta_SetMeta(GPS_LINKER, STA_LINKER_OFF);
}


#else
/* Structure to hold GPS work information */
static struct device *gps_dev;
static atomic_t gps_is_enabled;
static atomic_t gps_is_active;
static struct k_work_q *app_work_q;
static struct k_delayed_work start_work;
static struct k_delayed_work stop_work;
static int gps_reporting_interval_seconds;

static void start(struct k_work *work)
{
	ARG_UNUSED(work);
	int err;
	struct gps_config gps_cfg = {
		.nav_mode = GPS_NAV_MODE_PERIODIC,
		.power_mode = GPS_POWER_MODE_DISABLED,
		.timeout = CONFIG_GPS_CONTROL_FIX_TRY_TIME,
		.interval = CONFIG_GPS_CONTROL_FIX_TRY_TIME +
			gps_reporting_interval_seconds
	};

	if (gps_dev == NULL) {
		LOG_ERR("GPS controller is not initialized properly");
		return;
	}

#ifdef CONFIG_GPS_CONTROL_PSM_ENABLE_ON_START
	LOG_INF("Enabling PSM");

	err = lte_lc_psm_req(true);
	if (err) {
		LOG_ERR("PSM request failed, error: %d", err);
	} else {
		LOG_INF("PSM enabled");
	}
#endif /* CONFIG_GPS_CONTROL_PSM_ENABLE_ON_START */

	err = gps_start(gps_dev, &gps_cfg);
	if (err) {
		LOG_ERR("Failed to enable GPS, error: %d", err);
		return;
	}

	atomic_set(&gps_is_enabled, 1);
	gps_control_set_active(true);
	ui_led_set_pattern(UI_LED_GPS_SEARCHING);

	LOG_INF("GPS started successfully. Searching for satellites ");
	LOG_INF("to get position fix. This may take several minutes.");
	LOG_INF("The device will attempt to get a fix for %d seconds, ",
		CONFIG_GPS_CONTROL_FIX_TRY_TIME);
	LOG_INF("before the GPS is stopped. It's restarted every %d seconds",
		gps_reporting_interval_seconds);
#if !defined(CONFIG_GPS_SIM)
#if IS_ENABLED(CONFIG_GPS_START_ON_MOTION)
	LOG_INF("or as soon as %d seconds later when movement occurs.",
		CONFIG_GPS_CONTROL_FIX_CHECK_INTERVAL);
#endif
#endif
}

static void stop(struct k_work *work)
{
	ARG_UNUSED(work);
	int err;

	if (gps_dev == NULL) {
		LOG_ERR("GPS controller is not initialized");
		return;
	}

#ifdef CONFIG_GPS_CONTROL_PSM_DISABLE_ON_STOP
	LOG_INF("Disabling PSM");

	err = lte_lc_psm_req(false);
	if (err) {
		LOG_ERR("PSM mode could not be disabled, error: %d",
			err);
	}
#endif /* CONFIG_GPS_CONTROL_PSM_DISABLE_ON_STOP */

	err = gps_stop(gps_dev);
	if (err) {
		LOG_ERR("Failed to disable GPS, error: %d", err);
		return;
	}

	atomic_set(&gps_is_enabled, 0);
	gps_control_set_active(false);
	LOG_INF("GPS operation was stopped");
}

bool gps_control_is_enabled(void)
{
	return atomic_get(&gps_is_enabled);
}

void gps_control_enable(void)
{
#if !defined(CONFIG_GPS_SIM)
	atomic_set(&gps_is_enabled, 1);
	gps_control_start(1000);
#endif
}

void gps_control_disable(void)
{
#if !defined(CONFIG_GPS_SIM)
	atomic_set(&gps_is_enabled, 0);
	gps_control_stop(0);
	ui_led_set_pattern(UI_CLOUD_CONNECTED);
#endif
}
bool gps_control_is_active(void)
{
	return atomic_get(&gps_is_active);
}

bool gps_control_set_active(bool active)
{
	return atomic_set(&gps_is_active, active ? 1 : 0);
}

void gps_control_start(u32_t delay_ms)
{
	k_delayed_work_submit_to_queue(app_work_q, &start_work,
				       K_MSEC(delay_ms));
}

void gps_control_stop(u32_t delay_ms)
{
	k_delayed_work_submit_to_queue(app_work_q, &stop_work,
				       K_MSEC(delay_ms));
}

int gps_control_get_gps_reporting_interval(void)
{
	return gps_reporting_interval_seconds;
}

/** @brief Configures and starts the GPS device. */
int gps_control_init(struct k_work_q *work_q, gps_event_handler_t handler)
{
	int err;
	static bool is_init;

	if (is_init) {
		return -EALREADY;
	}

	if ((work_q == NULL) || (handler == NULL)) {
		return -EINVAL;
	}

	app_work_q = work_q;

	gps_dev = device_get_binding(CONFIG_GPS_DEV_NAME);
	if (gps_dev == NULL) {
		LOG_ERR("Could not get %s device",
			log_strdup(CONFIG_GPS_DEV_NAME));
		return -ENODEV;
	}

	err = gps_init(gps_dev, handler);
	if (err) {
		LOG_ERR("Could not initialize GPS, error: %d", err);
		return err;
	}

	k_delayed_work_init(&start_work, start);
	k_delayed_work_init(&stop_work, stop);

#if !defined(CONFIG_GPS_SIM)
	gps_reporting_interval_seconds =
		IS_ENABLED(CONFIG_GPS_START_ON_MOTION) ?
		CONFIG_GPS_CONTROL_FIX_CHECK_OVERDUE :
		CONFIG_GPS_CONTROL_FIX_CHECK_INTERVAL;
#else
	gps_reporting_interval_seconds = CONFIG_GPS_CONTROL_FIX_CHECK_INTERVAL;
#endif

	LOG_INF("GPS initialized");

	is_init = true;

	return err;
}
#endif
