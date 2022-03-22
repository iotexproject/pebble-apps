/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *
 * @brief   GPS module for asset tracker
 */

#ifndef GPS_CONTROLLER_H__
#define GPS_CONTROLLER_H__

#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_NRF9160_GPS
#define EXTERN_GPS  1
#endif

/*  Hardware board V2.0 added new extern GPS model */
/*  define  dev and  controller IO  */
#define UART_GPS  "UART_3"   /*  uart 3 used for gps  */

#if(CONFIG_IOTEX_BOARD_VERSION == 3)
#define GPS_EN       7          /*   gpio0.3  */
#elif (CONFIG_IOTEX_BOARD_VERSION == 2)
#define GPS_EN       3          /*   gpio0.3  */
#endif

void exGPSInit(void);
void exGPSStart(void);
void exGPSStop(void);
int getGPS(double *lat, double *lon);
void gpsSleep(void);
void gpsWakeup(void);
int32_t searchingSatelliteTime(void);
uint32_t getSatelliteSearchingTime(void);

#ifdef __cplusplus
}
#endif

#endif /* GPS_CONTROLLER_H__ */
