# Riverrock

The Riverrock connects an pebble board to the [IoTex IoTT Portal](https://app.iott.network/) via LTE-M/NB-IOT, transmits GPS and sensor data, and retrieves information about the device.


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
Riverrock runs in Power Saving Mode (PSM). In this mode, the device requests the PSM feature from the cellular network to save power.The device maintains a continuous cellular link and transmits data regularly.You can download the new configuration to pebble in the [IoTT Portal App Store](https://iotex-new-pebble-pr-9.onrender.com/apps). 

## Requirements

* IoTex pebble development board v3.0
* [IoTex sdk(Based on Nordic nRF-SDK v1.4.0)](https://github.com/iotexproject/pebble-firmware-legacy/tree/v1.4.0)
  
## Download configuration

After pebble works normally, press and hold the up and down buttons at the same time, pebble will start to connect to the network. After the network connection is complete, pebble prompts to start downloading the configuration on the portal, the device will enter the download configuration state, and keep this state for a maximum of 5 minutes.

## Developer documentation

Please refer to: https://iotex.larksuite.com/wiki/wikuseJV1r3A75SvxKR7DbtbxAd
