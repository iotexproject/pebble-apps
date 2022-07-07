#
# Copyright (c) 2019 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

zephyr_include_directories(.)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42605/icm426xx_pedometer.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42605/icm426xx_smdConfig.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42605/icm426xx_tiltConfig.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42605/icm426xx_womConfig.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42605/icm426xxActionDetect.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42605/Icm426xxDriver_HL.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42605/Icm426xxDriver_HL_apex.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42605/Icm426xxTransport.c)

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42607/inv_imu_apex.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42607/inv_imu_driver.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42607/inv_imu_transport.c)
#target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42607/inv_imu_pedometer.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm42607/inv_imu_wom.c)

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/icm_chip_helper.c)