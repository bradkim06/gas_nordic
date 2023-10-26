/**
 * @file bme680.c
 * @brief
 * @author bradkim02
 * @version v0.01
 * @date 2023-09-18
 */
#include <math.h>

#include <zephyr/logging/log.h>

#include <drivers/bme68x_iaq.h>

#include "bme680_app.h"
#include "bluetooth.h"
#include "hhs_math.h"

#if defined(CONFIG_BME68X)
LOG_MODULE_REGISTER(bme680, CONFIG_APP_LOG_LEVEL);

#define IAQ_UNHEALTHY_THRES 100
#define VOC_UNHEALTHY_THRES 2
#define CO2_UNHEALTHY_THRES 1000

struct k_sem temp_sem;

const struct sensor_trigger trig = {
	.chan = SENSOR_CHAN_ALL,
	.type = SENSOR_TRIG_TIMER,
};

struct bme680_data bme680 = {0};

static void adjustValuePrecision(int n)
{
	int32_t multiplier = pow(10, n);
	bme680.temp.val2 /= multiplier;
	bme680.press.val2 /= multiplier;
	bme680.humidity.val2 /= multiplier;
}

static void trigger_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	static bool is_init = true;
	static uint32_t events = 0;

	// sensor_sample_fetch(dev);
	sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &bme680.temp);
	sensor_channel_get(dev, SENSOR_CHAN_PRESS, &bme680.press);
	sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &bme680.humidity);
#if defined(CONFIG_BME68X_IAQ)
	sensor_channel_get(dev, SENSOR_CHAN_IAQ, &bme680.iaq);
	sensor_channel_get(dev, SENSOR_CHAN_CO2, &bme680.eCO2);
	sensor_channel_get(dev, SENSOR_CHAN_VOC, &bme680.breathVOC);
#endif
	adjustValuePrecision(4);
	if (is_init) {
		is_init = false;
		k_sem_give(&temp_sem);
	}

	LOG_DBG("temp: %d.%02d°C; press: %d.%02dPa; humidity: %d.%02d%%", bme680.temp.val1,
		bme680.temp.val2, bme680.press.val1, bme680.press.val2, bme680.humidity.val1,
		bme680.humidity.val2);
#if defined(CONFIG_BME68X_IAQ)

	LOG_DBG("iaq: %d(acc:%d); CO2: %dppm VOC: %d.%dppm", bme680.iaq.val1, bme680.iaq.val2,
		bme680.eCO2.val1, bme680.breathVOC.val1, bme680.breathVOC.val2);

	uint32_t curr_events = 0;

	if (bme680.iaq.val2 > 1 && bme680.iaq.val1 > IAQ_UNHEALTHY_THRES) {
		curr_events |= IAQ_VAL_THRESH;
	}

	if (bme680.breathVOC.val1 > VOC_UNHEALTHY_THRES) {
		curr_events |= VOC_VAL_THRESH;
	}

	if (bme680.eCO2.val1 > CO2_UNHEALTHY_THRES) {
		curr_events |= CO2_VAL_THRESH;
	}

	if (events != curr_events) {
		k_event_post(&bt_event, events);
		events = curr_events;
	}
#endif
};

int bme680_mon(void)
{
	const struct device *const dev = DEVICE_DT_GET_ANY(bosch_bme68x);
	if (!device_is_ready(dev)) {
		LOG_ERR("device is not ready");
		return 0;
	}

	k_sem_init(&temp_sem, 0, 1);
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
#define PRIORITY  7
K_THREAD_DEFINE(bme680_id, STACKSIZE, bme680_mon, NULL, NULL, NULL, PRIORITY, 0, 0);
#endif
