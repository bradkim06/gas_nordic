/*
 * Copyright (c) 2018-2019 Peter Bigot Consulting, LLC
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include "battery.h"

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

LOG_MODULE_REGISTER(BATTERY_MON, CONFIG_ADC_LOG_LEVEL);

/** A discharge curve specific to the power source. */
static const struct battery_level_point levels[] = {
	/* "Curve" here eyeballed from captured data for the [Adafruit
	 * 3.7v 2000 mAh](https://www.adafruit.com/product/2011) LIPO
	 * under full load that started with a charge of 3.96 V and
	 * dropped about linearly to 3.58 V over 15 hours.  It then
	 * dropped rapidly to 3.10 V over one hour, at which point it
	 * stopped transmitting.
	 *
	 * Based on eyeball comparisons we'll say that 15/16 of life
	 * goes between 3.95 and 3.55 V, and 1/16 goes between 3.55 V
	 * and 3.1 V.
	 */

	{10000, 3950},
	{625, 3550},
	{0, 3100},
};

void battmon(void)
{
	int rc = battery_measure_enable(true);

	if (rc != 0) {
		LOG_ERR("Failed initialize battery measurement: %d", rc);
		return;
	}

	// battery stable time delay
	k_msleep(2 * MSEC_PER_SEC);

	while (1) {
		/* Burn battery so you can see that this works over time */
		int batt_mV = battery_sample();
		unsigned int batt_pptt = battery_level_pptt(batt_mV, levels);

		LOG_INF("%d mV; %u pptt", batt_mV, batt_pptt);

		k_msleep(10 * MSEC_PER_SEC);
	}
}

K_THREAD_DEFINE(battmon_id, STACKSIZE, battmon, NULL, NULL, NULL, PRIORITY, 0, 0);
