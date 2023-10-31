# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
set_property(GLOBAL PROPERTY CSTD c11)

SET(CONF_FILE release.conf)
SET(BOARD_ROOT ${CMAKE_SOURCE_DIR}) 
SET(BOARD hhs_nrf52832)
SET(BOARD_FLASH_RUNNER jlink)
SET(APP_ROOT ${CMAKE_SOURCE_DIR}) 
list(APPEND ZEPHYR_EXTRA_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers
)

add_compile_definitions(_XOPEN_SOURCE=700)

find_package(Zephyr 3.4.99 EXACT)
project(gas_ces)

zephyr_include_directories(include)
FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
