# Riverrock

Riverrock is a pebble tracker application. Riverrock can upload GPS, ambient temperature, humidity sensors, motion sensors and other data to w3bstream. You can add devices and view sensor data in [IoTex IoTT Portal](https://app.iott.network/).

## Download firmware via usb

Refer [here](https://docs.iotex.io/machinefi/web3-smart-devices/pebble-tracker/firmware-update#offline-firmware-upgrade) to downloading the app firmware to the pebble.

## Download and compile the code

### Compile firmware locally

Since the app has secure boot enabled, if you need to compile the app firmware locally, you can only burn the merge.hex firmware via JTAG.When merge.hex is downloaded, the key is cleared from pebble, so pebble cannot connect to the iotex portal.

#### 1. Download nrf sdk v2.4.0

Please refer to the [Nordic documentation](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/installation/assistant.html) to install the nRF Connect SDK.

#### 2. Download pebble board configurations

```shell
cd  nrf_sdk_v240/nrf/boards/arm
git  clone -b  pebble_board_configure_for_sdk-2.4.0   https://github.com/iotexproject/pebble-firmware.git
mv  pebble-firmware/iotex_pebble_hw30  . && rm -rf pebble-firmware
```

#### 3. Download riverrock app

```shell
cd  nrf_sdk_v240/nrf/applications
git   clone -b riverrock https://github.com/iotexproject/pebble-apps.git

mv  pebble-apps/nrf/applications/Riverrock   . && rm -rf pebble-apps


```

#### 4. Apply patches

nrf sdk 2.4.0 prompts a compile error when disabling secure output for TF-M images. You can use the [patch](https://github.com/nrfconnect/sdk-nrf/pull/11886), or copy files directly from the patch directory to the sdk. 

```shell
cd Riverrock  

cp  patch/tfm/CMakeLists.txt  ../../modules/tfm/tfm/boards/
cp  patch/tfm/dummy_tfm_sp_log_raw.c   ../../modules/tfm/tfm/boards/common/

```

#### 5. Build

* To compile an application from the command line refer [here](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/config_and_build/programming.html)

* Compiling apps with vscode refer [here](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-VS-Code)

Note: We choose board 'iotex_pebble_hw30ns'.

eg.  Compiling the Riverrock app.

```shell
cd  nrf_sdk_v240/nrf/applications/Riverrock
west build -b iotex_pebble_hw30ns
```

## More Development Documentation

Please refer to: https://docs.iotex.io/verifiable-data/pebble-tracker/
