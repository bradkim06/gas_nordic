/**
 * @file bme680.c
 * @brief
 * @author bradkim02
 * @version v0.01
 * @date 2023-09-18
 */
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>

#include <drivers/bme68x_iaq.h>

LOG_MODULE_REGISTER(bme680, CONFIG_APP_LOG_LEVEL);

const struct sensor_trigger trig = {
	.chan = SENSOR_CHAN_ALL,
	.type = SENSOR_TRIG_TIMER,
};

static void trigger_handler(const struct device *dev,
			    const struct sensor_trigger *trig)
{
	struct sensor_value temp, press, humidity, iaq;

	// sensor_sample_fetch(dev);
	sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	sensor_channel_get(dev, SENSOR_CHAN_PRESS, &press);
	sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &humidity);
	sensor_channel_get(dev, SENSOR_CHAN_IAQ, &iaq);

	LOG_INF("temp: %d.%02d; press: %d.%02d; humidity: %d.%02d; iaq: %d",
		temp.val1, temp.val2, press.val1, press.val2,
		humidity.val1, humidity.val2, iaq.val1);
};

int bme680_mon(void)
{
	const struct device *const dev = DEVICE_DT_GET_ANY(bosch_bme680);
	if (!device_is_ready(dev)) {
		LOG_ERR("device is not ready");
		return 0;
	}

	k_sleep(K_SECONDS(3));

	int ret = sensor_trigger_set(dev, &trig, trigger_handler);

	if (ret) {
		LOG_ERR("couldn't set trigger");
		return 0;
	}

	return 0;
}


/* size of stack area used by each thread */
#define STACKSIZE 1024
/* scheduling priority used by each thread */
#define PRIORITY 7
K_THREAD_DEFINE(bme680_id, STACKSIZE, bme680_mon, NULL, NULL, NULL, PRIORITY, 0, 0);
