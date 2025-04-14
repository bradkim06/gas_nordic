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
GAS_COEFFICIENT_STRUCT();
/* Current value of the gas sensor. */
static struct gas_sensor_value gas_data[3];
static bool is_temperature_invalid;
static int32_t batt_adc = 0;

#define DATA_BUFFER_SIZE 30 // 데이터 버퍼 크기
#define SIGMA_MULTIPLIER 3  // 3-시그마 규칙을 위한 승수

typedef struct {
	int32_t buffer[DATA_BUFFER_SIZE];
	int index;
	bool is_full;
} CircularBuffer;

static CircularBuffer adc_buffer[2] = {0}; // O2와 GAS를 위한 두 개의 버퍼

// 1 mV/sec 변화
#define O2_DERIVATIVE_THRESHOLD 16.0f
#define O2_BASELINE_TOLERANCE   40 // 기준값과 차이가 40 mV(0.5%) 이하이면 보정 필요

// 동적 보정 함수: 최근 평균값과 이전 평균값의 변화율이 작으면 보정 실행
static void dynamic_oxygen_calibration(int32_t current_avg)
{
	static int32_t prev_o2_avg = 0;
	static int64_t boot_time = 0; // 부팅 시점 기록 (초)
	static int64_t prev_time = 0; // 마지막 측정 시각 (초)

	int64_t now = k_uptime_get() / 1000; // 현재 시간을 초 단위로 얻음
	if (boot_time == 0) {
		boot_time = now;
	}

	int expected_o2_raw =
		measurement_range[O2][0].lvl_mV * 20.9 / 25; // 기준 보정 전의 예상 raw 값

	// 부팅 후 초기 1분은 무조건 보정
	if ((now - boot_time) < 60) {
		LOG_INF("Initial dynamic O2 calibration (boot phase): current_avg=%d", current_avg);
		calibrate_oxygen("20.9", strlen("20.9"));
	} else if (abs(measurement_range[O2][0].lvl_mV - DEFAULT_O2_VALUE) <= 300) {
		float dt = (float)(now - prev_time);
		float derivative = (current_avg - prev_o2_avg) / dt;
		LOG_DBG("O2 derivative : %.2f mV/s", derivative);

		// 조건을 만족하면 보정 실행
		if (fabsf(derivative) < O2_DERIVATIVE_THRESHOLD &&
		    abs(current_avg - expected_o2_raw) < O2_BASELINE_TOLERANCE) {
			LOG_INF("Dynamic O2 calibration triggered: derivative=%.2f, current_avg=%d",
				derivative, current_avg);
			calibrate_oxygen("20.9", strlen("20.9"));
		}
	}

	prev_o2_avg = current_avg;
	prev_time = now;
}

// offset 변화율 임계값 (mV/s), 작으면 안정적임
#define GAS_OFFSET_DERIVATIVE_THRESHOLD 3.0f
#define GAS_OFFSET_DIFF_TOLERANCE       15 // offset 차이가 이 값보다 작아야 보정 진행
#define GAS_REFERENCE_VOLTAGE           610 // mV

static void dynamic_gas_offset_calibration(int32_t adc_value_mv)
{
	static int offset = 0;
	static int64_t boot_time = 0; // 부팅 시점 기록 (초)
	static int64_t last_time = 0;
	static int prev_offset = 0;

	int64_t now = k_uptime_get() / 1000;
	if (boot_time == 0) {
		boot_time = now;
	}

	int new_offset = -adc_value_mv;

	// 부팅 후 초기 1분은 무조건 보정
	if ((now - boot_time) < 60) {
		offset = new_offset;
		LOG_INF("Initial GAS offset calibration (boot phase): offset = %d", offset);
	} else if (abs(GAS_REFERENCE_VOLTAGE + new_offset) <= 50) {
		float dt = (float)(now - last_time);
		float offset_derivative = (float)(new_offset - prev_offset) / dt;
		LOG_DBG("Gas derivative: %.2f mV/s", offset_derivative);

		// 조건을 만족하면 보정 실행
		if (abs(new_offset - offset) < GAS_OFFSET_DIFF_TOLERANCE &&
		    fabsf(offset_derivative) < GAS_OFFSET_DERIVATIVE_THRESHOLD) {
			offset = new_offset;
			LOG_INF("Dynamic GAS offset updated: new offset = %d", offset);
		}
	}

	prev_offset = offset;
	last_time = now;
	gas_data[TEST].raw = abs(adc_value_mv + offset);
}

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
	if (is_temperature_invalid) {
		return raw_mv;
	}

	struct bme680_data env_data = get_bme680_data();
	unsigned int temperature_celsius = env_data.temp.val1 * 100 + env_data.temp.val2;
	float temp_coeff = (float)calculate_level_pptt(temperature_celsius, coeff_levels[gas_type]);

	temp_coeff = 1000.0f / temp_coeff;
	int32_t calibrated_mv = (int32_t)round(raw_mv * temp_coeff);
	LOG_DBG("Temperature coefficient : %f, Raw millivolts : %d, Calibrated millivolts : %d",
		temp_coeff, raw_mv, calibrated_mv);
	return calibrated_mv;
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

		// Ensure thread safety when updating the gas data
		k_sem_take(&gas_sem, K_FOREVER);
		gas_data[device_type].raw = avg_millivolt;
		gas_data[device_type].val1 = current_level / 10;
		gas_data[device_type].val2 = current_level % 10;
		k_sem_give(&gas_sem);
	} break;
	case GAS: {
		static int previous_gas_level = 0;
		const int GAS_THRESHOLD = 2;

		if (abs(current_level - previous_gas_level) > GAS_THRESHOLD) {
			is_gas_data_updated = true;
			previous_gas_level = current_level;
		}

		// Ensure thread safety when updating the gas data
		k_sem_take(&gas_sem, K_FOREVER);
		gas_data[device_type].raw = avg_millivolt;
		gas_data[device_type].val1 = current_level / 10;
		gas_data[device_type].val2 = current_level % 10;
		k_sem_give(&gas_sem);
	} break;
	default:
		// TODO: Handle the case for other types.
		break;
	}

	return is_gas_data_updated;
}

static void add_to_buffer(CircularBuffer *cb, int32_t value)
{
	cb->buffer[cb->index] = value;
	cb->index = (cb->index + 1) % DATA_BUFFER_SIZE;
	if (cb->index == 0) {
		cb->is_full = true;
	}
}

static float calculate_mean(CircularBuffer *cb)
{
	int32_t sum = 0;
	int count = cb->is_full ? DATA_BUFFER_SIZE : cb->index;
	for (int i = 0; i < count; i++) {
		sum += cb->buffer[i];
	}
	return (float)sum / count;
}

static float calculate_std_dev(CircularBuffer *cb, float mean)
{
	float sum_squared_diff = 0;
	int count = cb->is_full ? DATA_BUFFER_SIZE : cb->index;
	for (int i = 0; i < count; i++) {
		float diff = cb->buffer[i] - mean;
		sum_squared_diff += diff * diff;
	}
	return sqrt(sum_squared_diff / count);
}

static int32_t apply_3_sigma_rule(CircularBuffer *cb, int32_t value)
{
	if (!cb->is_full) {
		return value; // 버퍼가 가득 차지 않았으면 원래 값 반환
	}

	float mean = calculate_mean(cb);
	float std_dev = calculate_std_dev(cb, mean);
	float lower_bound = mean - SIGMA_MULTIPLIER * std_dev;
	float upper_bound = mean + SIGMA_MULTIPLIER * std_dev;

	if (value < lower_bound || value > upper_bound) {
		return (int32_t)mean; // 이상치를 평균값으로 대체
	}
	return value;
}

static void perform_adc_measurement(const struct adc_dt_spec *adc_channel_spec,
				    moving_average_t *gas_moving_avg,
				    enum gas_device gas_device_type)
{
	int16_t adc_buffer_value = 0;
	struct adc_sequence adc_sequence = {
		.buffer = &adc_buffer_value,
		.buffer_size = sizeof(adc_buffer_value),
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

	int32_t adc_value_mv = convert_adc_to_mv(adc_channel_spec, adc_buffer_value);

	if (adc_value_mv <= 0) {
		adc_value_mv = 0;
		// update_gas_data(adc_value_mv, gas_device_type);
		// return;
	}

	// int32_t calibrated_adc_value_mv = calculate_calibrated_mv(adc_value_mv, gas_device_type);

	// 3-시그마 규칙 적용
	add_to_buffer(&adc_buffer[gas_device_type], adc_value_mv);
	int32_t filtered_value = apply_3_sigma_rule(&adc_buffer[gas_device_type], adc_value_mv);

	if (gas_device_type == GAS) {
		// 배터리 ADC 값은 별도로 읽은 batt_adc 값으로 설정되어 있음.
		// batt_adc는 이미 갱신된 값이라고 가정.
		// 동적 offset 보정을 실행하여 gas_data[TEST].raw를 업데이트
		dynamic_gas_offset_calibration(filtered_value);
		filtered_value = gas_data[TEST].raw;
	} else {
		dynamic_oxygen_calibration(filtered_value);
	}

	int32_t average_mv = calculate_moving_average(gas_moving_avg, filtered_value);

	if (update_gas_data(filtered_value, gas_device_type)) {
		LOG_INF("O2 value changed %d.%d%%", gas_data[gas_device_type].val1,
			gas_data[gas_device_type].val2);
		k_event_post(&bt_event, GAS_VAL_CHANGE);
	}

	if (gas_device_type == O2) {
		// 동적 보정 판단: 변화율 및 기준값 차이를 확인

		LOG_DBG("%s - channel %d: "
			"mV filtered %" PRId32 "mV average %" PRId32 "mV %d.%d%%",
			enum_to_str(gas_device_type), adc_channel_spec->channel_id, filtered_value,
			average_mv, gas_data[gas_device_type].val1, gas_data[gas_device_type].val2);
	} else if (gas_device_type == GAS) {
		LOG_DBG("%s - channel %d: "
			"mV filtered %" PRId32 "mV average %" PRId32 "mV %d.%dppm",
			enum_to_str(gas_device_type), adc_channel_spec->channel_id, filtered_value,
			average_mv, gas_data[gas_device_type].val1, gas_data[gas_device_type].val2);
	}
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

void calibrate_gas(char *reference_value, int len)
{
	// Create a buffer to hold the reference value string plus a null terminator
	uint8_t str[len + 1];
	// Copy the reference value into the buffer and ensure it's null-terminated
	snprintf(str, len + 1, "%s",
		 reference_value); // Fixed the format specifier from "s" to "%s"

	// Convert the reference value string to a floating-point number
	float reference_ppm = atof(str);
	// 20ppm(NO2)
	reference_ppm = 20.0f / reference_ppm;

	// VDIFF = ISENSOR * RF(100k)
	unsigned int new_mV = gas_data[GAS].raw * reference_ppm;

	// Acquire the semaphore to ensure exclusive access to shared resources
	k_sem_take(&gas_sem, K_FOREVER);

	// Update the oxygen sensor's measurement range in millivolts
	measurement_range[GAS][0].lvl_mV = new_mV;

	// Release the semaphore
	k_sem_give(&gas_sem);

	// Update the sensor configuration with the new calibration value
	update_config(NO2_CALIBRATION, &new_mV);
	// Post an event to indicate that the oxygen sensor has been calibrated
	k_event_post(&config_event, NO2_CALIBRATION);
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
	float voltage = gas_data[O2].raw / ((1 + 200) * (reference_percent * 0.001 * 100));
	// Round down the voltage to two decimal places
	voltage = floor(voltage * 100) / 100;

	// Calculate the new maximum measurement value in millivolts for the oxygen sensor
	unsigned int new_mV = (voltage * 25 * 0.001 * 100) * (1 + 200);

	// Acquire the semaphore to ensure exclusive access to shared resources
	k_sem_take(&gas_sem, K_FOREVER);

	// Update the oxygen sensor's measurement range in millivolts
	measurement_range[O2][0].lvl_mV = new_mV;

	// Release the semaphore
	k_sem_give(&gas_sem);

	// Update the sensor configuration with the new calibration value
	update_config(OXYGEN_CALIBRATION, &new_mV);
	// Post an event to indicate that the oxygen sensor has been calibrated
	k_event_post(&config_event, OXYGEN_CALIBRATION);
}

/**
 * @brief Gas sensor thread function.
 *
 * This function configures ADC channels, sets up moving averages, and performs gas sensor
 * measurements. It reads and processes gas sensor data based on temperature compensation and checks
 * for changes.
 *
 * thread period current consumption test result
 * 1Sec = 11uA
 * 2Sec = 5uA
 * 3Sec = 3uA
 */
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif // DT Node assert
static void gas_measurement_thread(void)
{
	const uint8_t GAS_MEASUREMENT_INTERVAL_SEC = 2;
	const uint8_t GAS_AVERAGE_FILTER_SIZE = 1;
	/* Data of ADC io-channels specified in devicetree. */
	const struct adc_dt_spec gas_adc_channels[] = {
		// o2
		ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
		// gas
		ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
		// battery test
		ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),
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
	measurement_range[O2][0].lvl_mV = *(int16_t *)get_config(OXYGEN_CALIBRATION);
	measurement_range[GAS][0].lvl_mV = *(int16_t *)get_config(NO2_CALIBRATION);
	/* Unlock the mutex as the initialization is complete. */
	k_mutex_unlock(&config_mutex);
	LOG_WRN("test]] o2=%d gas=%d", measurement_range[O2][0].lvl_mV,
		measurement_range[GAS][0].lvl_mV);

	/* Wait for temperature data to become available. */
	if (k_sem_take(&temperature_semaphore, K_SECONDS(20)) != 0) {
		LOG_WRN("Temperature Input data not available!");
		is_temperature_invalid = true;
	} else {
		LOG_INF("Gas temperature sensing ok");
		is_temperature_invalid = false;
	}

	int16_t adc_buffer_value = 0;
	struct adc_sequence adc_sequence = {
		.buffer = &adc_buffer_value,
		.buffer_size = sizeof(adc_buffer_value),
	};

	int error = adc_sequence_init_dt(&gas_adc_channels[TEST], &adc_sequence);
	if (error < 0) {
		LOG_WRN("Could not Init ADC channel (%d)", error);
		return;
	}

	while (1) {
		// O2 채널 측정 및 moving average 계산
		perform_adc_measurement(&gas_adc_channels[O2], gas_moving_avg[O2], O2);

		// GAS 채널 측정
		perform_adc_measurement(&gas_adc_channels[GAS], gas_moving_avg[GAS], GAS);

		k_sleep(K_SECONDS(GAS_MEASUREMENT_INTERVAL_SEC));
	}
}

#define STACKSIZE 1024
#define PRIORITY  4
K_THREAD_DEFINE(gas_id, STACKSIZE, gas_measurement_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
