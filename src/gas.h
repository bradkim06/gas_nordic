#ifndef __APP_GAS_H__
#define __APP_GAS_H__

#include "hhs_util.h"

#define DEVICE_LIST(X)                                                                             \
	/* O2 gas sensor*/                                                                         \
	X(O2, = 0)                                                                                 \
	/* optional select gas sensor(H2S, CO, NH3, SO2) are common */                             \
	X(GAS, )
DECLARE_ENUM(gas_device, DEVICE_LIST)

#define GAS_LEVEL_POINT_STRUCT()                                                                   \
	/* Discharge curve specific to the gas source. */                                          \
	static const struct level_point measurement_range[2][2] = {                                \
		/* O2 */                                                                           \
		{                                                                                  \
			/*  Measurement Range Max 25% Oxygen */                                    \
			{250, 662}, /*  Zero current (offset) <0.6 % vol O2 */                     \
			{0, 0},                                                                    \
		}, /*  TODO Gas */                                                                 \
		{},                                                                                \
	}

#define GAS_COEFFICIENT_STRUCT()                                                                   \
	/* curve specific to the gas temperature coefficient. */                                   \
	static const struct level_point coeff_levels[2][5] = {                                     \
		/* Output Temperature Coefficient Oxygen Sensor */                                 \
		{                                                                                  \
			{10500, 5000},                                                             \
			{10400, 4000},                                                             \
			{10000, 2000},                                                             \
			{9600, 0},                                                                 \
			{9000, -2000},                                                             \
		}, /* TODO Output Temperature Coefficient Gas Sensor */                            \
		{},                                                                                \
	}

struct gas_sensor_value {
	/** Integer part of the value. */
	unsigned int val1;
	/** Fractional part of the value (in one-millionth parts). */
	unsigned int val2;
};

/**
 * @brief Get the gas sensor data.
 *
 * This function takes a semaphore to ensure exclusive access to 'curr_result',
 * makes a copy of the current gas sensor data, then releases the semaphore.
 *
 * @param gas_dev The gas device to get data from.
 * @return The copied gas sensor data.
 */
struct gas_sensor_value get_gas_data(enum gas_device gas_dev);

#endif // __APP_GAS_H__
