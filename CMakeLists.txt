# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
set_property(GLOBAL PROPERTY CSTD c17)
set(BOARD hhs_nrf52832)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gas_ces)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
