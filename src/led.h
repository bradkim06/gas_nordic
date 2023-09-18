#ifndef __APP_LED_H__
#define __APP_LED_H__

#include "enum_macro.h"
#include <stdbool.h>

#define LED_DEVICE(X)                                                                              \
	X(state_g, = 0)                                                                            \
	X(lowbatt_y, )

DECLARE_ENUM(led_dev, LED_DEVICE);

int led_ctrl(enum led_dev color, bool power);

#endif
