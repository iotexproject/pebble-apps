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

/*  gps Mosaic ? */
#define  USE_GPS_MOSAIC     1


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

static  struct device *guart_dev_gps;
struct device *gpsPower;
static u8_t uart_buf1[128];
static u8_t pos1=0;
static uint8_t  bGpsHead = 0;
const char strGRMC[7] = "$GNRMC";

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
static struct k_work gpsPackageParse;
static double gpsLat = 200.0, gpsLon = 200.0;
static uint8_t bgpsAtive = 0;
const uint8_t searchTime[] = {30,40,50,60,70,80,90,100,110,120};
static uint8_t timeIndex = 0;
static  uint8_t legalSatelliteTime = 30;

static int getRMC(char *nmea, gprmc_t *loc);

void gpsPackageParseHandle(struct k_work *work) {
    gprmc_t rmc;
    int intpart;
    double fractpart;
    double lat,lon;

    if (!getRMC(uart_buf1, &rmc)) {
        /*  lat  */
        intpart = rmc.latitude/100;
        fractpart = ((rmc.latitude / 100) - intpart) / 0.6;
        lat = intpart+fractpart;
        /*  lon */
        intpart = rmc.longitude / 100;
        fractpart = ((rmc.longitude / 100)-intpart) / 0.6;
        lon = intpart + fractpart;
        /*  South West ? */
        if (rmc.lat == 'S')
            lat *= (-1);
        if (rmc.lon == 'W')
            lon *= (-1);
        sys_mutex_lock(&iotex_gps_mutex, K_FOREVER);
        gpsLat = lat;
        gpsLon = lon;
        bgpsAtive = 1;
        sys_mutex_unlock(&iotex_gps_mutex);
        sta_SetMeta(GPS_LINKER, STA_LINKER_ON);
    } else {
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
    if (!bGpsHead) {
        if (0x24 == character) {
            pos1 = 1;
            bGpsHead = 1;
            uart_buf1[0] = character;
        }
    } else {
        uart_buf1[pos1] = character;
        if ((uart_buf1[pos1 - 1] == '\r') && (character == '\n')) {
            uart_buf1[pos1 - 1] ='\n'; /* delete '\r',printk  '\n' to '\r'+'\n' */
            uart_buf1[pos1] = 0;
            if (strncmp(uart_buf1, strGRMC, strlen(strGRMC)) == 0) {
                uart_irq_rx_disable(guart_dev_gps);
                /* printk("%s",uart_buf1); */
                k_work_submit(&gpsPackageParse);
            }
            pos1 = 0;
            bGpsHead = 0;
        } else {
            pos1++;
            if (pos1 > (sizeof(uart_buf1) - 1)) {
                pos1 = 0;
                bGpsHead = 0;    
            } else {
                if (pos1 == strlen(strGRMC)) {
                    if (strncmp(uart_buf1, strGRMC, strlen(strGRMC))) {
                        pos1 = 0;
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
    /* GNRMC,084852.000,A,2236.9453,N,11408.4790,E,0.53,292.44,141216,,,A*75 */
    char *p = nmea;
    char *end = nmea + strlen(nmea);
    char buf[20] = { 0 };
    CHECK_POINT(p, strchr(p, ','), end);
    CHECK_POINT(p, strchr(p, ','), end);
    if (p[0] != 'A')
        return -1;

    CHECK_POINT(p, strchr(p, ','), end);
    memcpy(buf, p, strchr(p, ',') - p);
    if (strlen(buf) == 0)
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
    memset(buf, 0, sizeof(buf));
    memcpy(buf, p, strchr(p, ',') - p);
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
static void gpsCmdSet(uint8_t *buf, uint32_t len) {
    uint32_t i;
    for(i = 0; i < len; i++)
    {
        uart_poll_out(guart_dev_gps,buf[i]); 
    }
}

int getGPS(double *lat, double *lon) {
    int ret = -1;
    sys_mutex_lock(&iotex_gps_mutex, K_FOREVER);
    if (bgpsAtive) {
        *lat = gpsLat;
        *lon = gpsLon;
        ret = 0;
    }
    sys_mutex_unlock(&iotex_gps_mutex);
    return ret;
}

void exGPSStart(void) {
    uart_irq_rx_enable(guart_dev_gps);
}

void exGPSStop(void) {
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
    gpio_pin_write(gpsPower, GPS_EN, 1);
}

void gpsPowerOff(void) {    
    gpio_pin_write(gpsPower, GPS_EN, 0);
}

void gpsSleep(void) {
    /* $PMTK161,0*28<CR><LF> */
    const uint8_t cmd_buf[]={0x24,0x50,0x4d,0x54,0x4b,0x31,0x36,0x31,0x2c,0x30,0x2a,0x32,0x38,0x0d,0x0a};
    gpsCmdSet(cmd_buf, sizeof(cmd_buf));
}

void gpsWakeup(void) {
    const uint8_t cmd_buf[]={0x0d,0x0a};
    gpsCmdSet(cmd_buf, sizeof(cmd_buf));
}
/* GPS active or not*/
bool  isGPSActive(void) {
    return (bgpsAtive == 1);
}

int32_t searchingSatelliteTime(void) {
    if(!isGPSActive()) {
        timeIndex++;
        if(timeIndex > sizeof(searchTime))
            timeIndex = sizeof(searchTime) -1 ;
    }
    if(searchTime[timeIndex] > iotex_mqtt_get_upload_period())
        legalSatelliteTime = iotex_mqtt_get_upload_period();
    else
        legalSatelliteTime = searchTime[timeIndex];
    return k_sleep(K_SECONDS(legalSatelliteTime));
}
uint32_t getSatelliteSearchingTime(void) {
    return (legalSatelliteTime);
}


