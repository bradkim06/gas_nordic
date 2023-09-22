#!/bin/sh

nrfjprog --version
nrfjprog -f nrf52 --chiperase --reset --program ./build/zephyr/zephyr.hex
