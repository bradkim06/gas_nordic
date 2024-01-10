/**
 * @file src/bme680_app.c - bme680 environments sensor application
 *
 * @brief Code for collecting environmental sensor information through the BME680 driver.
 *
 * @author bradkim06@gmail.com
 */
#include <math.h>

#include <zephyr/logging/log.h>

#include <drivers/bme68x_iaq.h>

#include "bme680_app.h"
#if defined(CONFIG_BME68X_IAQ_EN)
#include "bluetooth.h"
#endif // CONFIG_BME68X_IAQ_EN

/* Register the BME680 module with the specified log level. */
LOG_MODULE_REGISTER(bme680, CONFIG_APP_LOG_LEVEL);

/* Define a semaphore for synchronization during initial gas data temperature correction. */
struct k_sem temperature_semaphore;

#if defined(CONFIG_BME68X)
/**
 * Define various air quality warning thresholds and provide detailed information
 * for each parameter in the README.md.
 */
#define IAQ_UNHEALTHY_THRES 100
#define VOC_UNHEALTHY_THRES 2
#define CO2_UNHEALTHY_THRES 1000

/* Define a semaphore for mutual exclusion of BME680 data. */
K_SEM_DEFINE(bme680_sem, 1, 1);

/* Define a sensor trigger for timer-based sampling of all channels. */
const struct sensor_trigger trigger = {
	.type = SENSOR_TRIG_TIMER,
	.chan = SENSOR_CHAN_ALL,
};

/* Initialize the BME680 data structure with default values. */
struct bme680_data bme680 = {0};

/**
 * @brief Truncates the number of decimal places in the sensor data received from the Zephyr sensor.
 * The default valid range for decimal data is 6 digits, but this can be adjusted by specifying
 * the number of decimal places to truncate.
 *
 * @param sensor_data: The sensor data to truncate.
 * @param num_decimal_places: The number of decimal places to truncate.
 */
static void truncate_sensor_data_decimal_places(int32_t *sensor_data, int num_decimal_places)
{
	// Check for invalid input.
	if (*sensor_data <= 0 || num_decimal_places < 0) {
		// It is possible case
		LOG_WRN("invalid input parameter, sensord_data : %d num_decimal_places : %d",
			*sensor_data, num_decimal_places);
		*sensor_data = 0;
		return;
	}

	// Calculate the number of digits in the sensor data.
	const int num_digits = (int)(floor(log10(*sensor_data))) + 1;

	// Check if the number of decimal places to truncate is greater than the number of digits in
	// the sensor data.
	if (num_decimal_places > num_digits) {
		LOG_ERR("invalid input num_decimal_places, %d", num_decimal_places);
		return;
	}

	// Calculate the factor to divide the sensor data by to truncate the number of decimal
	// places.
	int truncation_factor = (int)(pow(10, num_digits - num_decimal_places));

	// Truncate the number of decimal places in the sensor data.
	*sensor_data /= truncation_factor;
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
#endif // CONFIG_BME68X_IAQ_EN

	truncate_sensor_data_decimal_places(&bme680.temp.val2, 2);
	truncate_sensor_data_decimal_places(&bme680.press.val2, 2);
	truncate_sensor_data_decimal_places(&bme680.humidity.val2, 2);

	// Release the BME680 semaphore
	k_sem_give(&bme680_sem);

	// If this is the first time the function is called, release the temperature semaphore
	if (is_init && bme680.temp.val1 > 0) {
		is_init = false;
		k_sem_give(&temperature_semaphore);
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
	static uint32_t events = 0;
	if (events != curr_events) {
		k_event_post(&bt_event, events);
		events = curr_events;
	}
#endif // CONFIG_BME68X_IAQ_EN
};

/**
 * @brief Function to get BME680 sensor data.
 *
 * This function ensures exclusive access to the sensor data by taking a semaphore,
 * creates a copy of the sensor data, and then releases the semaphore.
 *
 * @return A copy of the BME680 sensor data.
 */
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

#endif // CONFIG_BME68X

/**
 * @brief The BME680 thread function runs only once and performs two tasks:
 *
 * 1.Initializes the Bosch BME68x device and registers the trigger handler.
 * 2.Initializes the temperature semaphore so that when temperature data is available,
 * the gas sensor can start operating
 * (the gas sensor's results are calibrated based on temperature)
 */
static void bme680_thread_function(void)
{
	// Get the device structure for the Bosch BME68x sensor
	const struct device *const bme68x_device = DEVICE_DT_GET_ANY(bosch_bme68x);

	// Check if the device is ready
	if (!device_is_ready(bme68x_device)) {
		LOG_ERR("BME68x device is not ready");
		return;
	}

	// Initialize the temperature semaphore with initial value of 0 and maximum value of 1
	k_sem_init(&temperature_semaphore, 0, 1);

	// Sleep for 1 second to allow the device to initialize
	k_sleep(K_SECONDS(1));

	// Set the trigger for the device and register the trigger handler
	int trigger_set_status = sensor_trigger_set(bme68x_device, &trigger, trigger_handler);
	if (trigger_set_status) {
		LOG_ERR("Failed to set trigger for BME68x device");
		return;
	}
}

#define STACKSIZE 1024
#define PRIORITY  3
K_THREAD_DEFINE(bme680_id, STACKSIZE, bme680_thread_function, NULL, NULL, NULL, PRIORITY, 0, 0);
