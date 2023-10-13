# Copyright (c) 2018 Bosch Sensortec GmbH
# Copyright (c) 2022, Leonard Pollak
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()

zephyr_library_sources(bme68x.c bme68x_i2c.c)
