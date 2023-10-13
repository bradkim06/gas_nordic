/**
 * @file bme680.c
 * @brief
 * @author bradkim06
 * @version v0.01
 * @date 2023-09-18
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "bme680_app.h"

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

LOG_MODULE_REGISTER(BME680_MON, CONFIG_BOARD_HHS_LOG_LEVEL);

struct bme680 bme680_result;

static void bme680_mon(void)
{
	const struct device *const dev = DEVICE_DT_GET_ONE(bosch_bme68x);
	if (!device_is_ready(dev)) {
		LOG_ERR("device not ready.");
		return;
	}

	struct bme680 *p = &bme680_result;
	LOG_INF("Device %s ready", dev->name);

	while (1) {
		k_sleep(K_SECONDS(60));

		sensor_sample_fetch(dev);
		sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &p->temp);
		sensor_channel_get(dev, SENSOR_CHAN_PRESS, &p->press);
		sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &p->humidity);
		sensor_channel_get(dev, SENSOR_CHAN_GAS_RES, &p->gas_res);

		LOG_INF("T: %d.%06d; P: %d.%06d; H: %d.%06d; G: %d.%06d", p->temp.val1,
			p->temp.val2, p->press.val1, p->press.val2, p->humidity.val1,
			p->humidity.val2, p->gas_res.val1, p->gas_res.val2);
	}
}

K_THREAD_DEFINE(bme680_id, STACKSIZE, bme680_mon, NULL, NULL, NULL, PRIORITY, 0, 0);
