#ifndef __APP_GAS_H__
#define __APP_GAS_H__

#include "hhs_util.h"

#define DEVICE_LIST(X)                                                                             \
	/* O2 gas sensor*/                                                                         \
	X(O2, = 0)                                                                                 \
	/* optional select gas sensor(H2S, CO, NH3, SO2) are common */                             \
	X(GAS, )
DECLARE_ENUM(gas_device, DEVICE_LIST)

struct gas_sensor_value {
	/** Integer part of the value. */
	unsigned int val1;
	/** Fractional part of the value (in one-millionth parts). */
	unsigned int val2;
};

/**
 * @brief Get gas sensor data for the specified device.
 *
 * This function retrieves gas sensor data by taking a semaphore to ensure mutual exclusion,
 * makes a copy of the data, and then releases the semaphore.
 *
 * @param dev The gas sensor device for which to retrieve data.
 * @return A copy of the gas sensor data for the specified device.
 */
struct gas_sensor_value get_gas_data(enum gas_device dev);

#endif
