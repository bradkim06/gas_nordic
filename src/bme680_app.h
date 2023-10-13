#ifndef _APP_BME680_H_
#define _APP_BME680_H_

#include <zephyr/drivers/sensor.h>

struct bme680 {
	struct sensor_value temp;
	struct sensor_value press;
	struct sensor_value humidity;
	struct sensor_value gas_res;
};

#endif
