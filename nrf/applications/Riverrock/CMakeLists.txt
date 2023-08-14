#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

cmake_minimum_required(VERSION 3.8.2)

set(PM_STATIC_YML_FILE ${CMAKE_CURRENT_SOURCE_DIR}/configuration/${BOARD}/pm_static.yml)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(asset_tracker)
zephyr_compile_definitions(PROJECT_NAME=${PROJECT_NAME})
zephyr_compile_definitions(_POSIX_C_SOURCE=200809L)

get_target_property(TFM_BINARY_DIR tfm TFM_BINARY_DIR)
configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/secure_peripheral_partition/tfm_manifest_list.yaml.in
  ${CMAKE_CURRENT_BINARY_DIR}/secure_peripheral_partition/tfm_manifest_list.yaml
)

set_property(TARGET zephyr_property_target
  APPEND PROPERTY TFM_CMAKE_OPTIONS
  -DTFM_EXTRA_MANIFEST_LIST_FILES=${CMAKE_CURRENT_BINARY_DIR}/secure_peripheral_partition/tfm_manifest_list.yaml
  -DTFM_EXTRA_PARTITION_PATHS=${CMAKE_CURRENT_LIST_DIR}/secure_peripheral_partition
)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)
# BME680
target_sources(app PRIVATE src/bme/bme680.c)
target_sources(app PRIVATE src/bme/bme680_helper.c)

# HAL
target_sources(app PRIVATE src/hal/hal_adc.c)
target_sources(app PRIVATE src/hal/hal_gpio.c)

# Modem
target_sources(app PRIVATE src/modem/modem_helper.c)
target_sources(app PRIVATE src/modem/modemStorage.c)
# MQTT
target_sources(app PRIVATE src/mqtt/mqtt.c)
target_sources(app PRIVATE src/mqtt/config.c)
target_sources(app PRIVATE src/mqtt/payload.c)
target_sources(app PRIVATE src/mqtt/devReg.c)
# Protbuf
target_sources(app PRIVATE src/nanopb/pb_encode.c)
target_sources(app PRIVATE src/nanopb/pb_decode.c)
target_sources(app PRIVATE src/nanopb/pb_common.c)
target_sources(app PRIVATE src/nanopb/package.pb.c)
# Local NVS
target_sources(app PRIVATE src/nvs/local_storage.c)
# Signature 
target_sources(app PRIVATE src/sign/ecdsa.c)
target_sources(app PRIVATE src/sign/secure_peripheral_partition.c)
# OTA
target_sources(app PRIVATE src/ota/http_update.c)
target_sources(app PRIVATE src/time/ntp.c)

target_sources(app PRIVATE src/light_sensor/tsl2572.c)
# NORDIC SDK APP END
zephyr_include_directories(src)

# Include application events and configuration headers
zephyr_library_include_directories(
  src/ui
  src/nanopb
  src/mqtt
  src/light_sensor
  src/watchdog
  src/sign
  src/icm
  )

# Application sources
add_subdirectory(src/ui)
add_subdirectory(src/gps_controller)
add_subdirectory_ifdef(CONFIG_WATCHDOG src/watchdog)
add_subdirectory(src/icm)

target_include_directories(app PRIVATE
  $<TARGET_PROPERTY:tfm,TFM_BINARY_DIR>/install/interface/include
)

target_compile_definitions(app
    PRIVATE TFM_PARTITION_SECURE_PERIPHERAL_PARTITION
)

if (CONFIG_USE_BME680_BSEC)
  target_link_libraries(app PUBLIC bsec_lib)
endif()
