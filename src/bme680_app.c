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

/* Register the BME680 module with the specified log level. */
LOG_MODULE_REGISTER(bme680, CONFIG_APP_LOG_LEVEL);

/* Define a semaphore for mutual exclusion of BME680 data. */
K_SEM_DEFINE(bme680_sem, 1, 1);

/**
 * Define various air quality warning thresholds and provide detailed information
 * for each parameter in the README.md.
 */
#define IAQ_UNHEALTHY_THRES 100
#define VOC_UNHEALTHY_THRES 2
#define CO2_UNHEALTHY_THRES 1000

/* Define a semaphore for synchronization during initial gas data temperature correction. */
struct k_sem temp_sem;

/* Define a sensor trigger for timer-based sampling of all channels. */
const struct sensor_trigger trig = {
	.type = SENSOR_TRIG_TIMER,
	.chan = SENSOR_CHAN_ALL,
};

/* Initialize the BME680 data structure with default values. */
struct bme680_data bme680 = {0};

/**
 * @brief This function adjusts the precision of sensor data received from the Zephyr sensor.
 * The default valid range for decimal data is 6 digits, but this can be adjusted by specifying
 * the number of decimal places to truncate.
 *
 * @param n: The number of decimal places to truncate.
 */
static void adjustValuePrecision(int n)
{
	// Calculate the multiplier based on the specified number of decimal places.
	int32_t multiplier = pow(10, n);

	// Divide the sensor data values by the multiplier to truncate the decimal places.
	bme680.temp.val2 /= multiplier;
	bme680.press.val2 /= multiplier;
	bme680.humidity.val2 /= multiplier;
}

/**
 * @brief Callback function called at the BSEC library's sample rate
 *
 * This function is called at the BSEC library's sample rate and is responsible for retrieving both
 * BME680 and BSEC library measurement results. The retrieved data is stored in the bme680
 * structure, which can be accessed for detailed data. Additionally, if the IAQ, CO2, or VOC data
 * exceeds a threshold, a BLE event is triggered once.
 *
 * @param dev Pointer to the device structure
 * @param trig Pointer to the sensor trigger structure
 */
static void trigger_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	// Initialize static variables
	static bool is_init = true;
	static uint32_t events = 0;

	// Take the BME680 semaphore to ensure exclusive access to the sensor
	k_sem_take(&bme680_sem, K_FOREVER);

	// Retrieve temperature, pressure, and humidity data from the BME680 sensor
	sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &bme680.temp);
	sensor_channel_get(dev, SENSOR_CHAN_PRESS, &bme680.press);
	sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &bme680.humidity);

	// If IAQ is enabled, retrieve IAQ, CO2, and VOC data from the BSEC library
#if defined(CONFIG_BME68X_IAQ_EN)
	sensor_channel_get(dev, SENSOR_CHAN_IAQ, &bme680.iaq);
	sensor_channel_get(dev, SENSOR_CHAN_CO2, &bme680.eCO2);
	sensor_channel_get(dev, SENSOR_CHAN_VOC, &bme680.breathVOC);
#endif

	// Adjust the precision of the retrieved data to 4 decimal places
	adjustValuePrecision(4);

	// Release the BME680 semaphore
	k_sem_give(&bme680_sem);

	// If this is the first time the function is called, release the temperature semaphore
	if (is_init) {
		is_init = false;
		k_sem_give(&temp_sem);
	}

	// Print the retrieved data to the debug log
	LOG_DBG("temp: %d.%02d°C; press: %d.%02dPa; humidity: %d.%02d%%", bme680.temp.val1,
		bme680.temp.val2, bme680.press.val1, bme680.press.val2, bme680.humidity.val1,
		bme680.humidity.val2);

	// If IAQ is enabled, print IAQ, CO2, and VOC data to the debug log
#if defined(CONFIG_BME68X_IAQ_EN)
	LOG_DBG("iaq: %d(acc:%d); CO2: %dppm VOC: %d.%dppm", bme680.iaq.val1, bme680.iaq.val2,
		bme680.eCO2.val1, bme680.breathVOC.val1, bme680.breathVOC.val2);

	// Initialize a variable to store the current events
	uint32_t curr_events = 0;

	// If the IAQ value is greater than 1 and the IAQ accuracy is greater than the unhealthy
	// threshold, set the IAQ value threshold event
	if (bme680.iaq.val2 > 1 && bme680.iaq.val1 > IAQ_UNHEALTHY_THRES) {
		curr_events |= IAQ_VAL_THRESH;
	}

	// If the VOC value is greater than the unhealthy threshold, set the VOC value threshold
	// event
	if (bme680.breathVOC.val1 > VOC_UNHEALTHY_THRES) {
		curr_events |= VOC_VAL_THRESH;
	}

	// If the CO2 value is greater than the unhealthy threshold, set the CO2 value threshold
	// event
	if (bme680.eCO2.val1 > CO2_UNHEALTHY_THRES) {
		curr_events |= CO2_VAL_THRESH;
	}

	// If the current events are different from the previous events, post a BLE event and update
	// the events variable
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
	// Get the device structure for the Bosch BME68x sensor
	const struct device *const dev = DEVICE_DT_GET_ANY(bosch_bme68x);

	// Check if the device is ready
	if (!device_is_ready(dev)) {
		LOG_ERR("device is not ready");
		return;
	}

	// Initialize the temperature semaphore with initial value of 0 and maximum value of 1
	k_sem_init(&temp_sem, 0, 1);

	// Sleep for 1 second to allow the device to initialize
	k_sleep(K_SECONDS(1));

	// Set the trigger for the device and register the trigger handler
	int ret = sensor_trigger_set(dev, &trig, trigger_handler);
	if (ret) {
		LOG_ERR("couldn't set trigger");
		return;
	}
}

struct bme680_data get_bme680_data(void)
{
	/* Take the semaphore to ensure exclusive access to the sensor data */
	k_sem_take(&bme680_sem, K_FOREVER);
	/* Create a copy of the sensor data */
	struct bme680_data copy = bme680;
	/* Release the semaphore to allow other processes to access the sensor data */
	k_sem_give(&bme680_sem);

	/* Return the copy of the sensor data */
	return copy;
}

#define STACKSIZE 1024
#define PRIORITY  7
K_THREAD_DEFINE(bme680_id, STACKSIZE, bme680_thread_fn, NULL, NULL, NULL, PRIORITY, 0, 0);
#endif
