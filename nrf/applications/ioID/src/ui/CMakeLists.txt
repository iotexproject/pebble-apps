#
# Copyright (c) 2019 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

zephyr_include_directories(include)

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/keyBoard.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/display.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/status_bar.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/hint.c)
