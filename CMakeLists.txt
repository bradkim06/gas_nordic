# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
set_property(GLOBAL PROPERTY CSTD c11)

set(BOARD_ROOT ${CMAKE_SOURCE_DIR})
set(BOARD hhs_nrf52832)
# set(BOARD_FLASH_RUNNER jlink)
set(APP_ROOT ${CMAKE_SOURCE_DIR})
set(ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/drivers)

set(CONF_FILE "prj.conf")
list(APPEND CONF_FILE "debug.conf")

add_compile_definitions(_XOPEN_SOURCE=700)

find_package(Zephyr 3.5.99 EXACT)
project(gas_ces)

zephyr_include_directories(include)
file(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
