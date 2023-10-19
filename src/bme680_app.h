#ifndef _APP_BME680_H_
#define _APP_BME680_H_

#include <zephyr/drivers/sensor.h>

struct bme680_iaq {
	struct sensor_value temp;
	struct sensor_value press;
	struct sensor_value humidity;
	struct sensor_value iaq;
};

extern struct bme680_iaq bme680;

#endif
