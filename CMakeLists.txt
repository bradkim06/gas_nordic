# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
set_property(GLOBAL PROPERTY CSTD c17)
set(BOARD_ROOT ${CMAKE_SOURCE_DIR}) 
set(APP_ROOT ${CMAKE_SOURCE_DIR}) 
set(BOARD hhs_nrf52832)
list(APPEND ZEPHYR_EXTRA_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/drivers
)

find_package(Zephyr 3.3.99 EXACT)
project(gas_ces)

zephyr_include_directories(include)
FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
