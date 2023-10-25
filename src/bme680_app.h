#ifndef __APP_BME680_H__
#define __APP_BME680_H__

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

extern struct k_sem temp_sem;

#if defined(CONFIG_BME68X)
struct bme680_data {
	struct sensor_value temp;
	struct sensor_value press;
	struct sensor_value humidity;
#if defined(CONFIG_BME68X_IAQ)
	struct sensor_value iaq;
	struct sensor_value eCO2;      // ppm
	struct sensor_value breathVOC; // ppm
#endif
};

extern struct bme680_data bme680;

#endif
#endif
