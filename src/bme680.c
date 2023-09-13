/*
 * Copyright (c) 2023 Libre Solar Technologies GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "switch.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

LOG_MODULE_REGISTER(BME680_MON, CONFIG_SENSOR_LOG_LEVEL);

static void bme680_mon(void)
{
	const struct device *const dev = DEVICE_DT_GET_ONE(bosch_bme680);
	struct sensor_value temp, press, humidity, gas_res;

	switch_ctrl(BME680_SENSOR_EN, true);

	if (!device_is_ready(dev)) {
		LOG_ERR("device not ready.");
		return;
	}

	LOG_INF("Device %p name is %s", dev, dev->name);

	while (1) {
		k_sleep(K_MSEC(3000));

		sensor_sample_fetch(dev);
		sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(dev, SENSOR_CHAN_PRESS, &press);
		sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &humidity);
		sensor_channel_get(dev, SENSOR_CHAN_GAS_RES, &gas_res);

		LOG_DBG("T: %d.%06d; P: %d.%06d; H: %d.%06d; G: %d.%06d", temp.val1, temp.val2,
			press.val1, press.val2, humidity.val1, humidity.val2, gas_res.val1,
			gas_res.val2);
	}
}

K_THREAD_DEFINE(bme680_id, STACKSIZE, bme680_mon, NULL, NULL, NULL, PRIORITY, 0, 0);
