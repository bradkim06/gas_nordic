/*
 * Copyright (c) 2023 Libre Solar Technologies GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

LOG_MODULE_REGISTER(BME680_MON, CONFIG_SENSOR_LOG_LEVEL);

#define SENSOR_LOAD_SW DT_NODELABEL(sw0)

#if !DT_NODE_EXISTS(SENSOR_LOAD_SW)
#error "Overlay for power output node not properly defined."
#endif

static const struct gpio_dt_spec load_switch = GPIO_DT_SPEC_GET_OR(SENSOR_LOAD_SW, gpios, {0});

static int load_sw_setup(void)
{
	int err;

	if (!gpio_is_ready_dt(&load_switch)) {
		LOG_ERR("The load switch pin GPIO port is not ready.\n");
		return -ENODEV;
	}

	LOG_INF("Initializing pin with inactive level.\n");

	err = gpio_pin_configure_dt(&load_switch, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("Configuring GPIO pin failed: %d\n", err);
		return err;
	}

	LOG_INF("load switch Waiting one second.\n");

	return 0;
}

SYS_INIT(load_sw_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static int load_sw_ctrl(bool power)
{
	int err = gpio_pin_set_dt(&load_switch, power);
	if (err != 0) {
		LOG_ERR("Setting GPIO pin level failed: %d\n", err);
		return err;
	}
	return 0;
}

static void bme680_mon(void)
{
	const struct device *const dev = DEVICE_DT_GET_ONE(bosch_bme680);
	struct sensor_value temp, press, humidity, gas_res;

	if (load_sw_ctrl(true)) {
		return;
	}

	if (!device_is_ready(dev)) {
		LOG_ERR("device not ready.\n");
		return;
	}

	LOG_INF("Device %p name is %s\n", dev, dev->name);

	while (1) {
		k_sleep(K_MSEC(3000));

		sensor_sample_fetch(dev);
		sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(dev, SENSOR_CHAN_PRESS, &press);
		sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &humidity);
		sensor_channel_get(dev, SENSOR_CHAN_GAS_RES, &gas_res);

		LOG_DBG("T: %d.%06d; P: %d.%06d; H: %d.%06d; G: %d.%06d\n", temp.val1, temp.val2,
			press.val1, press.val2, humidity.val1, humidity.val2, gas_res.val1,
			gas_res.val2);
	}
}

K_THREAD_DEFINE(bme680_id, STACKSIZE, bme680_mon, NULL, NULL, NULL, PRIORITY, 0, 0);
