#!/bin/sh

nrfjprog --version
nrfjprog -f nrf52 --chiperase --reset --verify --program ./build/zephyr/zephyr.hex
# nrfjprog --program /Users/bradkim06/hhs/docker/work/gas_ces/build/zephyr/zephyr.hex --chiperase --verify -f NRF52

