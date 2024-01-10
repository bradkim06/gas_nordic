/**
 * @file src/gas.c - electrochemical gas sensor adc application code
 *
 * @brief Program for Reading electrochemical Gas Sensor ADC Information Using Nordic's SAADC.
 *
 * This file contains the code for reading gas sensor values using the ADC interface, applying
 * temperature compensation, and calculating moving averages. It also checks for any changes in
 * gas sensor values and posts an event accordingly.
 *
 * @author
 * bradkim06@gmail.com
 */
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bluetooth.h"
#include "gas.h"
#include "hhs_math.h"
#include "hhs_util.h"
#include "bme680_app.h"
#include "settings.h"

/* Module registration for Gas Monitor with the specified log level. */
LOG_MODULE_REGISTER(GAS_MON, CONFIG_APP_LOG_LEVEL);

/* Enumeration of gas devices from the DEVICE_LIST. */
DEFINE_ENUM(gas_device, DEVICE_LIST)

/* Semaphore used for mutual exclusion of gas sensor data. */
K_SEM_DEFINE(gas_sem, 1, 1);

GAS_LEVEL_POINT_STRUCT();
// GAS_COEFFICIENT_STRUCT();
/* Current value of the gas sensor. */
static struct gas_sensor_value gas_data[2];

/**
 * @brief Converts ADC raw data to millivolts.
 *
 * This function takes the raw ADC data and converts it to millivolts based on the ADC channel
 * configuration. If the ADC channel is configured as differential, the raw data is directly
 * converted to millivolts. If the ADC channel is not differential, the raw data is checked to be
 * non-negative before conversion. The converted value is then passed to adc_raw_to_millivolts_dt()
 * for further conversion based on device tree specifications. If adc_raw_to_millivolts_dt() returns
 * an error, a warning is logged and the error code is returned.
 *
 * @param adc_channel Pointer to the ADC channel configuration structure.
 * @param raw_adc_data Raw ADC data to be converted.
 *
 * @return Converted value in millivolts if successful, error code otherwise.
 */
static int32_t convert_adc_to_mv(const struct adc_dt_spec *adc_channel, int16_t raw_adc_data)
{
	int32_t millivolts = (adc_channel->channel_cfg.differential)
				     ? (int32_t)((int16_t)raw_adc_data)
				     : (int32_t)raw_adc_data;

	int err = adc_raw_to_millivolts_dt(adc_channel, &millivolts);
	if (err < 0) {
		LOG_WRN("Value in millivolts not available");
		return err;
	}
	return millivolts;
}

/**
 * @brief Calculate calibrated millivolts for a given gas type.
 *
 * This function calculates the calibrated millivolts for a given gas type.
 * It uses the temperature coefficient based on the gas type to calibrate the millivolts.
 *
 * @param raw_mv The raw millivolts value.
 * @param gas_type The type of gas device (O2 or GAS).
 *
 * @return The calibrated millivolts value.
 */
static int32_t calculate_calibrated_mv(int32_t raw_mv, enum gas_device gas_type)
{
	// struct bme680_data env_data = get_bme680_data();
	// double temp_coeff;
	//
	// temp_coeff = ((double)calculate_level_pptt(env_data.temp.val1 * 100 + env_data.temp.val2,
	// 					   coeff_levels[gas_type]) /
	// 	      10000.f);
	//
	// int32_t calibrated_mv = (int32_t)round(raw_mv * temp_coeff);
	// LOG_DBG("Temperature coefficient : %f, Raw millivolts : %d, Calibrated millivolts : %d",
	// 	temp_coeff, raw_mv, calibrated_mv);
	return raw_mv;
}

/**
 * @brief Update gas data based on the average millivolt and gas device type.
 *
 * This function calculates the current gas level based on the average millivolt,
 * compares it with the previous value, and updates the gas data if the difference
 * exceeds a certain threshold. It also ensures thread safety when updating the gas data.
 *
 * @param avg_millivolt The average millivolt.
 * @param device_type The type of the gas device.
 *
 * @return True if the gas data is updated; false otherwise.
 */
static bool update_gas_data(int32_t avg_millivolt, enum gas_device device_type)
{
	bool is_gas_data_updated = false;
	int current_level = calculate_level_pptt(avg_millivolt, measurement_range[device_type]);

	switch (device_type) {
	case O2: {
		static int previous_o2_level = 0;
		const int O2_THRESHOLD = 2;

		if (abs(current_level - previous_o2_level) > O2_THRESHOLD) {
			is_gas_data_updated = true;
			previous_o2_level = current_level;
		}
	} break;
	case GAS: {
		static int previous_gas_level = 0;
		const int GAS_THRESHOLD = 2;

		if (abs(current_level - previous_gas_level) > GAS_THRESHOLD) {
			is_gas_data_updated = true;
			previous_gas_level = current_level;
		}
	} break;
	default:
		// TODO: Handle the case for other types.
		break;
	}

	// Ensure thread safety when updating the gas data
	k_sem_take(&gas_sem, K_FOREVER);
	gas_data[device_type].raw = avg_millivolt;
	gas_data[device_type].val1 = current_level / 10;
	gas_data[device_type].val2 = current_level % 10;
	k_sem_give(&gas_sem);

	return is_gas_data_updated;
}

/**
 * @brief Perform ADC measurement and update gas data
 *
 * This function performs an ADC measurement on the specified channel, calculates the moving
 * average, updates the gas data and logs the information.
 *
 * @param adc_channel Pointer to the ADC channel specification.
 * @param gas_moving_avg Pointer to the moving average data structure.
 * @param type The type of the gas device.
 */
static void perform_adc_measurement(const struct adc_dt_spec *adc_channel_spec,
				    moving_average_t *gas_moving_avg,
				    enum gas_device gas_device_type)
{
	int16_t adc_buffer = 0;
	struct adc_sequence adc_sequence = {
		.buffer = &adc_buffer,
		.buffer_size = sizeof(adc_buffer),
	};

	int error = adc_sequence_init_dt(adc_channel_spec, &adc_sequence);
	if (error < 0) {
		LOG_WRN("Could not Init ADC channel (%d)", error);
		return;
	}

	error = adc_read(adc_channel_spec->dev, &adc_sequence);
	if (error < 0) {
		LOG_WRN("Could not perform ADC read (%d)", error);
		return;
	}

	int32_t adc_value_mv = convert_adc_to_mv(adc_channel_spec, adc_buffer);
	if (adc_value_mv < 0) {
		LOG_WRN("Negative ADC values(%d) are not allowed. It will be converted to 0",
			adc_value_mv);
		adc_value_mv = 0;
	}

	int32_t calibrated_adc_value_mv = calculate_calibrated_mv(adc_value_mv, gas_device_type);
	int32_t average_mv = calculate_moving_average(gas_moving_avg, calibrated_adc_value_mv);

	if (update_gas_data(average_mv, gas_device_type)) {
		LOG_INF("O2 value changed %d.%d%%", gas_data[gas_device_type].val1,
			gas_data[gas_device_type].val2);
		k_event_post(&bt_event, GAS_VAL_CHANGE);
	}

	LOG_DBG("%s - channel %d: "
		" current %" PRId32 "mV average %" PRId32 "mV %d.%d%s",
		enum_to_str(gas_device_type), adc_channel_spec->channel_id, calibrated_adc_value_mv,
		average_mv, gas_data[gas_device_type].val1, gas_data[gas_device_type].val2,
		(gas_device_type == O2) ? "%" : "ppm");
}

/**
 * @brief Setup gas ADC
 *
 * This function configures individual channels before sampling.
 *
 * @param adc_channel ADC channel data structure
 *
 * @return 0 on success, negative error code otherwise
 */
static int setup_gas_adc(struct adc_dt_spec adc_channel)
{
	/* Check if the device is ready */
	if (!device_is_ready(adc_channel.dev)) {
		LOG_ERR("ADC controller device %s not ready", adc_channel.dev->name);
		return -ENODEV;
	}

	/* Setup the ADC channel */
	int setup_error = adc_channel_setup_dt(&adc_channel);
	if (setup_error < 0) {
		LOG_ERR("Could not setup channel #%d (%d)", adc_channel.channel_id, setup_error);
		return -EIO;
	}

	/* Return 0 on success */
	return 0;
}

struct gas_sensor_value get_gas_data(enum gas_device gas_dev)
{
	// function will block indefinitely until the semaphore is available.
	k_sem_take(&gas_sem, K_FOREVER);

	// Create a local copy of the gas sensor data for the specified gas device.
	struct gas_sensor_value gas_sensor_copy = gas_data[gas_dev];

	// Release the semaphore after the shared resource has been safely read.
	k_sem_give(&gas_sem);

	// Return the local copy of the gas sensor data.
	// This allows the caller to use the sensor data without worrying about concurrent access
	return gas_sensor_copy;
}

void calibrate_oxygen(char *reference_value, int len)
{
	// Create a buffer to hold the reference value string plus a null terminator
	uint8_t str[len + 1];
	// Copy the reference value into the buffer and ensure it's null-terminated
	snprintf(str, len + 1, "%s",
		 reference_value); // Fixed the format specifier from "s" to "%s"

	// Convert the reference value string to a floating-point number
	float reference_percent = atof(str);

	// Calculate the voltage based on the reference percent and the voltage divider
	// Note: The formula for voltage calculation is specific to the sensor and circuit design
	float voltage = gas_data[O2].raw / ((1 + 2000 / 10.7) * (reference_percent * 0.001 * 100));
	// Round down the voltage to two decimal places
	voltage = floor(voltage * 100) / 100;

	// Calculate the new maximum measurement value in millivolts for the oxygen sensor
	unsigned int new_mV = (voltage * 25 * 0.001 * 100) * (1 + 2000 / 10.7);

	// Acquire the semaphore to ensure exclusive access to shared resources
	k_sem_take(&gas_sem, K_FOREVER);

	// Update the oxygen sensor's measurement range in millivolts
	measurement_range[O2][0].lvl_mV = new_mV;

	// Release the semaphore
	k_sem_give(&gas_sem);

	// Update the sensor configuration with the new calibration value
	update_config(OXYGEN_CALIBRATION, new_mV);
	// Post an event to indicate that the oxygen sensor has been calibrated
	k_event_post(&config_event, OXYGEN_CALIBRATION);
}

/**
 * @brief Gas sensor thread function.
 *
 * This function configures ADC channels, sets up moving averages, and performs gas sensor
 * measurements. It reads and processes gas sensor data based on temperature compensation and checks
 * for changes.
 */
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif // DT Node assert
static void gas_measurement_thread(void)
{
	k_mutex_init(&config_mutex);
	k_condvar_init(&config_condvar);
	k_mutex_lock(&config_mutex, K_FOREVER);

	const uint8_t GAS_MEASUREMENT_INTERVAL_SEC = 1;
	const uint8_t GAS_AVERAGE_FILTER_SIZE = 60;
	/* Data of ADC io-channels specified in devicetree. */
	const struct adc_dt_spec gas_adc_channels[] = {
		// o2
		ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
		// gas
		ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
	};

	for (size_t idx = 0U; idx < ARRAY_SIZE(gas_adc_channels); idx++) {
		setup_gas_adc(gas_adc_channels[idx]);
	}

	/* Moving average filter for gas sensor data. */
	moving_average_t *gas_moving_avg[2];

	/* Allocate memory for moving averages. */
	for (int idx = 0; idx < 2; idx++) {
		gas_moving_avg[idx] = allocate_moving_average(GAS_AVERAGE_FILTER_SIZE);
		if (gas_moving_avg[idx] == NULL) {
			return;
		}
	}

	k_condvar_wait(&config_condvar, &config_mutex, K_FOREVER);
	measurement_range[O2][0].lvl_mV = get_config(OXYGEN_CALIBRATION);
	k_mutex_unlock(&config_mutex);

	// /* Wait for temperature data to become available. */
	// if (k_sem_take(&temperature_semaphore, K_SECONDS(3)) != 0) {
	// 	LOG_WRN("Temperature Input data not available!");
	// 	// TODO: Temperature sensor error case
	// } else {
	// 	/* Fetch available data. */
	// 	LOG_INF("Gas temperature sensing ok");
	// }

	while (1) {
		/* Perform gas sensor measurements. */
		perform_adc_measurement(&gas_adc_channels[O2], gas_moving_avg[O2], O2);
		/* Perform gas sensor measurements. */
		perform_adc_measurement(&gas_adc_channels[GAS], gas_moving_avg[GAS], GAS);

		/* Wait for the specified period of time. */
		k_sleep(K_SECONDS(GAS_MEASUREMENT_INTERVAL_SEC));
	}
}

#define STACKSIZE 1024
#define PRIORITY  4
K_THREAD_DEFINE(gas_id, STACKSIZE, gas_measurement_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
