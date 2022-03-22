# Aries

Aries is a pedometer based on Riverrock.

## Overview

The application uses the LTE link control driver to establish a network connection.
It will send motion data to IoTex's [IoTT Portal](https://app.iott.network/).
The data is visualized in IoTex's web interface.

### Sensor data

The collected data includes the GPS position, steps(read from motion sensor icm42605).

|  Sensor data       | Sensor type  |  Data unit  |
|  ----              | ----         |  ----       |
| GPS coordinates    | GPS          |  NMEA Gxxx string|
| Pedometer steps    | FLIP         |  steps      |
| Pedometer pace     | FLIP         |  steps/sec      |

### Working mode
Aries runs in Power Saving Mode (PSM). In this mode, the device requests the PSM feature from the cellular network to save power.The device maintains a continuous cellular link and transmits data every minute.

## Requirements

* IoTex pebble development board v3.0
* [IoTex sdk(Based on Nordic nRF-SDK v1.4.0)](https://github.com/iotexproject/pebble-firmware-legacy/tree/v1.4.0)

## Code modified based on riverrock
pedometer.c, pedometer.h :  Pedometer driver

### How to debug the driver of the pedometer
Open the macro definition DEBUG_PETOMETER in pedometer.c. You will get the following information in the log output from the serial port.

```
8 steps - cadence: 1.72 steps/sec - WALK
10 steps - cadence: 1.16 steps/sec - WALK

```


## Developer documentation

Please refer to: https://docs.iotex.io/verifiable-data/pebble-tracker/
