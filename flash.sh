#!/bin/sh

nrfjprog --version
nrfjprog -f nrf52 --chiperase --reset --verify --program ./build/zephyr/zephyr.hex
