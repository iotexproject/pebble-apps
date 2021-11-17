# Gravel

The Gravel connects an pebble board to the [IoTex IoTT Portal](https://app.iott.network/) via LTE-M/NB-IOT, transmits GPS and sensor data, and retrieves information about the device.


## Overview


The application uses the LTE link control driver to establish a network connection.
It then collects various data locally, and transmits the data to IoTex's [IoTT Portal](https://app.iott.network/).
The data is visualized in IoTex's web interface.

### Sensor data

The collected data includes the GPS position, accelerometer readings (the device's physical orientation), and data from various environment sensors.

|  Sensor data       | Sensor type  |  Data unit  |
|  ----              | ----         |  ----       |
| GPS coordinates    | GPS          |  NMEA Gxxx string|
| Accelerometer data | FLIP         |  String      |
| Temperature        | TEMP         |  Celsius     |
| Humidity           | HUMID        |  Percert     |
| Air pressure       | AIR_PRESS    |  Pascal      |
| Light sensor       | LIGHT        |  Lux         |

### Working mode

Gravel run in Power Saving Mode (PSM). In this mode, the device requests the PSM feature from the cellular network to save power.The device maintains a continuous cellular link and transmits data every 5 minutes.

## Requirements

* iotex Pebble development board v3.0
* [iotex sdk(Based on Nordic nRF-SDK v1.4.0)](https://github.com/iotexproject/pebble-firmware.git)
  


## Developer documentation

Please refer to: https://iotex.larksuite.com/wiki/wikuseJV1r3A75SvxKR7DbtbxAd
