/**
 * @file bme680.c
 * @brief
 * @author bradkim02
 * @version v0.01
 * @date 2023-09-18
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/bme68x_iaq.h>

#include "bme680_app.h"

#if defined(CONFIG_BME68X)
LOG_MODULE_REGISTER(bme680, CONFIG_APP_LOG_LEVEL);

const struct sensor_trigger trig = {
	.chan = SENSOR_CHAN_ALL,
	.type = SENSOR_TRIG_TIMER,
};

struct bme680_data bme680 = {0};

static void trigger_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	// sensor_sample_fetch(dev);
	sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &bme680.temp);
	sensor_channel_get(dev, SENSOR_CHAN_PRESS, &bme680.press);
	sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &bme680.humidity);
#if defined(CONFIG_BME68X_IAQ)
	sensor_channel_get(dev, SENSOR_CHAN_IAQ, &bme680.iaq);
	sensor_channel_get(dev, SENSOR_CHAN_CO2, &bme680.eCO2);
	sensor_channel_get(dev, SENSOR_CHAN_VOC, &bme680.breathVOC);
#endif

	LOG_DBG("temp: %d.%06d°C; press: %d.%06dPa; humidity: %d.%06d%%", bme680.temp.val1,
		bme680.temp.val2, bme680.press.val1, bme680.press.val2, bme680.humidity.val1,
		bme680.humidity.val2);
#if defined(CONFIG_BME68X_IAQ)
	LOG_DBG("iaq: %d(acc:%d); CO2: %dppm VOC: %dppm", bme680.iaq.val1, bme680.iaq.val2,
		bme680.eCO2.val1, bme680.breathVOC.val1);
#endif
};

int bme680_mon(void)
{
	const struct device *const dev = DEVICE_DT_GET_ANY(bosch_bme68x);
	if (!device_is_ready(dev)) {
		LOG_ERR("device is not ready");
		return 0;
	}

	k_sleep(K_SECONDS(1));

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
#define PRIORITY  8
K_THREAD_DEFINE(bme680_id, STACKSIZE, bme680_mon, NULL, NULL, NULL, PRIORITY, 0, 0);
#endif
