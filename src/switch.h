#ifndef APP_SWITCH_H
#define APP_SWITCH_H

#include "enum_macro.h"
#include <stdbool.h>

#define LOADSW_DEVICE(X)                                                                           \
	X(bme680, = 0)                                                                             \
	X(batt_mon_en, )

DECLARE_ENUM(loadsw_dev, LOADSW_DEVICE);

int switch_ctrl(enum loadsw_dev sw, bool power, bool wait);

#endif
