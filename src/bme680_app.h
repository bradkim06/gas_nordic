#ifndef _APP_BME680_H_
#define _APP_BME680_H_

#include <zephyr/drivers/sensor.h>

typedef struct bme680_iaq {
	struct sensor_value temp;
	struct sensor_value press;
	struct sensor_value humidity;
	struct sensor_value iaq;
} bme680_t;

extern bme680_t bme680;

#endif
