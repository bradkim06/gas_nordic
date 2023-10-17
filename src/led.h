#ifndef __APP_LED_H__
#define __APP_LED_H__

#include "hhs_util.h"
#include <stdbool.h>

#define LED_DEVICE(X)                                                                              \
	X(stablebatt_g, = 0)                                                                       \
	X(lowbatt_y, )

DECLARE_ENUM(led_dev, LED_DEVICE);

#endif
