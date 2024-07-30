#ifndef __APP_GAS_H__
#define __APP_GAS_H__

#include "settings.h"

#define GAS_LEVEL_POINT_STRUCT()                                                                   \
	/* Discharge curve specific to the gas source. */                                          \
	static struct level_point measurement_range[2][2] = {                                      \
		/* O2 */                                                                           \
		{                                                                                  \
			{250, DEFAULT_O2_VALUE},                                                   \
			/*  Zero current (offset) <0.6 % vol O2 */ {0, 0},                         \
		},                                                                                 \
		{                                                                                  \
			/*  Measurement Range Max 100ppm H2S*/                                     \
			{5000, 700},                                                               \
			{0, 50},                                                                   \
		},                                                                                 \
	}

#define GAS_COEFFICIENT_STRUCT()                                                                   \
	/* curve specific to the gas temperature coefficient. */                                   \
	static struct level_point coeff_levels[2][7] = {                                           \
		/* Output Temperature Coefficient Oxygen Sensor */                                 \
		{                                                                                  \
			{1030, 4000},                                                              \
			{1015, 3000},                                                              \
			{1000, 2000},                                                              \
			{975, 1000},                                                               \
			{950, 0},                                                                  \
			{920, -1000},                                                              \
			{890, -2000},                                                              \
		}, /* Output Temperature Coefficient Gas Sensor */                                 \
		{                                                                                  \
			{1030, 4000},                                                              \
			{1015, 3000},                                                              \
			{1000, 2000},                                                              \
			{975, 1000},                                                               \
			{950, 0},                                                                  \
			{920, -1000},                                                              \
			{890, -2000},                                                              \
		},                                                                                 \
	}

struct gas_sensor_value {
	/** adc raw data **/
	unsigned int raw;
	/** Integer part of the value. */
	unsigned int val1;
	/** Fractional part of the value (in one-millionth parts). */
	unsigned int val2;
};

/**
 * @brief This function is designed to be used in a multitasking environment where multiple threads
 * might try to access the `gas_data` array concurrently. The use of a semaphore (`gas_sem`) ensures
 * that only one thread can access the data at a time, preventing race conditions and ensuring data
 * integrity. The function blocks indefinitely until it can take the semaphore, makes a copy of the
 * data, and then releases the semaphore as quickly as possible.
 *
 * @param gas_dev The gas device to get data from.
 * @return The copied gas sensor data.
 */
struct gas_sensor_value get_gas_data(enum gas_device gas_dev);

/**
 * @brief Calibrates the oxygen sensor based on a reference value.
 *
 * This function calibrates the oxygen sensor by calculating a new millivolt level
 * for the sensor based on the provided reference value. It takes into account
 * the voltage divider formed by resistors R1 and R2 to adjust the sensor's
 * measurement range. It also ensures thread-safe access to shared resources using
 * a semaphore.
 *
 * @param reference_value A string representing the reference oxygen level in percent.
 * @param len The length of the reference_value string.
 */
void calibrate_oxygen(char *reference_value, int len);

#endif // __APP_GAS_H__
