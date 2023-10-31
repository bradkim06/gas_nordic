/**
 * @file src/bme680_app.c - bme680 environments sensor application
 *
 * @brief Code for collecting environmental sensor information through the BME680 driver.
 *
 * @author bradkim06@gmail.com
 */

#if defined(CONFIG_BME68X)
#include <math.h>

#include <zephyr/logging/log.h>

#include <drivers/bme68x_iaq.h>

#include "bme680_app.h"
#include "bluetooth.h"
#include "hhs_math.h"

LOG_MODULE_REGISTER(bme680, CONFIG_APP_LOG_LEVEL);
/* Used for Mutual Exclusion of BME680 data. */
K_SEM_DEFINE(bme680_sem, 1, 1);

/**
 * Various air quality warning thresholds and detailed information
 * for each parameter can be found in the README.md.
 */
#define IAQ_UNHEALTHY_THRES 100
#define VOC_UNHEALTHY_THRES 2
#define CO2_UNHEALTHY_THRES 1000

/* Semaphore for synchronization for initial gas data temperature correction. */
struct k_sem temp_sem;

const struct sensor_trigger trig = {
	.type = SENSOR_TRIG_TIMER,
	.chan = SENSOR_CHAN_ALL,
};

struct bme680_data bme680 = {0};

/**
 * @brief When receiving Zephyr sensor data, the default specified valid range for decimal data
 * (6 digits) is adjusted arbitrarily and rounded.
 *
 * @param n: The number of decimal places to truncate.
 */
static void adjustValuePrecision(int n)
{
	int32_t multiplier = pow(10, n);
	bme680.temp.val2 /= multiplier;
	bme680.press.val2 /= multiplier;
	bme680.humidity.val2 /= multiplier;
}

/**
 * @brief Callback function called at the BSEC library's sample rate
 *
 * Retrieves both BME680 and BSEC library measurement results. For detailed data, please refer to
 * the structure comments or README.md. Additionally, if IAQ, CO2, or VOC data exceeds a threshold,
 * a BLE event is triggered once.
 */
static void trigger_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	static bool is_init = true;
	static uint32_t events = 0;

	k_sem_take(&bme680_sem, K_FOREVER);
	sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &bme680.temp);
	sensor_channel_get(dev, SENSOR_CHAN_PRESS, &bme680.press);
	sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &bme680.humidity);
#if defined(CONFIG_BME68X_IAQ_EN)
	sensor_channel_get(dev, SENSOR_CHAN_IAQ, &bme680.iaq);
	sensor_channel_get(dev, SENSOR_CHAN_CO2, &bme680.eCO2);
	sensor_channel_get(dev, SENSOR_CHAN_VOC, &bme680.breathVOC);
#endif
	adjustValuePrecision(4);
	k_sem_give(&bme680_sem);
	if (is_init) {
		is_init = false;
		k_sem_give(&temp_sem);
	}

	LOG_DBG("temp: %d.%02d°C; press: %d.%02dPa; humidity: %d.%02d%%", bme680.temp.val1,
		bme680.temp.val2, bme680.press.val1, bme680.press.val2, bme680.humidity.val1,
		bme680.humidity.val2);
#if defined(CONFIG_BME68X_IAQ_EN)

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

/**
 * @brief The BME680 thread function runs only once and performs two tasks:
 *
 * 1.Initializes the Bosch BME68x device and registers the trigger handler.
 * 2.Initializes the temperature semaphore so that when temperature data is available,
 * the gas sensorcan start operating
 * (the gas sensor's results are calibrated based on temperature)
 */
void bme680_thread_fn(void)
{
	const struct device *const dev = DEVICE_DT_GET_ANY(bosch_bme68x);
	if (!device_is_ready(dev)) {
		LOG_ERR("device is not ready");
		return;
	}

	k_sem_init(&temp_sem, 0, 1);
	k_sleep(K_SECONDS(1));

	int ret = sensor_trigger_set(dev, &trig, trigger_handler);
	if (ret) {
		LOG_ERR("couldn't set trigger");
		return;
	}
}

struct bme680_data get_bme680_data(void)
{
	k_sem_take(&bme680_sem, K_FOREVER);
	struct bme680_data copy = bme680;
	k_sem_give(&bme680_sem);

	return copy;
}

#define STACKSIZE 1024
#define PRIORITY  7
K_THREAD_DEFINE(bme680_id, STACKSIZE, bme680_thread_fn, NULL, NULL, NULL, PRIORITY, 0, 0);
#endif
