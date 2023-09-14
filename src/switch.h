#ifndef APP_SWITCH_H
#define APP_SWITCH_H

#include <stdbool.h>

enum load_switch {
	BME680_SENSOR_EN = 0,
	BATT_MON_EN,
	LOW_BATT_INDICATOR,
};

int switch_ctrl(enum load_switch sw, bool power, bool wait);

#endif
