#ifndef __APP_GAS_H__
#define __APP_GAS_H__

#include "hhs_util.h"

#define DEVICE_LIST(X)                                                                             \
	X(O2, = 0)                                                                                 \
	X(GAS, )

DECLARE_ENUM(gas_device, DEVICE_LIST)

struct gas_sensor_value {
	/** Integer part of the value. */
	unsigned int val1;
	/** Fractional part of the value (in one-millionth parts). */
	unsigned int val2;
};

struct gas_sensor_value get_gas_data(enum gas_device dev);

#endif
