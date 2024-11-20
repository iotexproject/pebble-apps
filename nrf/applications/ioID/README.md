# Riverrock

Riverrock is a pebble tracker application. Riverrock can upload GPS, ambient temperature, humidity sensors, motion sensors and other data to w3bstream. You can add devices and view sensor data in [IoTex IoTT Portal](https://app.iott.network/) You can add devices and view sensor data in [IoTex IoTT Portal]().


# Download firmware via usb

Refer [here][https://docs.iotex.io/machinefi/web3-smart-devices/pebble-tracker/firmware-update#offline-firmware-upgrade] by downloading the app firmware into pebble.


# Download and compile code

## Compile firmware locally
Since the app has secure boot enabled, if you need to compile the app firmware locally, you can only burn the merge.hex firmware via JTAG.

### 1. Download nrf sdk v2.4.0
Download nrf sdk v2.4.0 and toolchains [here](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/installation/assistant.html) (nRF Connect for Desktop is recommended).

### 2. Download Riverrock app
```shell
git   clone -b riverrock https://github.com/iotexproject/pebble-apps.git

cd Riverrock
sed  -i   's/set(PM_STATIC_YML_FILE /#set(PM_STATIC_YML_FILE /g'   CMakeLists.txt

```

### 3. Apply patches
nrf sdk 2.4.0 prompts a compile error when disabling secure output for TF-M images. You can use the [patch](https://github.com/nrfconnect/sdk-nrf/pull/11886), or copy files directly from the patch directory to the sdk. 

```shell
cd Riverrock  

cp  patch/tfm/CMakeLists.txt  nrf_sdk_v240/nrf/modules/tfm/tfm/boards/
cp  patch/tfm/dummy_tfm_sp_log_raw.c   nrf_sdk_v240/nrf/modules/tfm/tfm/boards/common/

```


### 4. Build

* To compile an application from the command line refer [here](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/config_and_build/programming.html)

* Compiling apps with vscode refer [here](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-VS-Code)


## More Development Documentation

Please refer to: https://docs.iotex.io/verifiable-data/pebble-tracker/
