/**
 * @file src/bme680_data.h
 * @brief This file contains the structure for BME680 sensor data.
 *
 * The BME680 sensor measures temperature, pressure, humidity, IAQ, eCO2, and breathVOC.
 * The average current consumption is 3.7 µA at 1 Hz for humidity, pressure, and temperature,
 * and 0.09‒12 mA for p/h/T/gas depending on the operation mode.
 */
#ifndef __APP_BME680_H__
#define __APP_BME680_H__

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

extern struct k_sem temperature_semaphore;

#if defined(CONFIG_BME68X)
/**
 * @struct bme680_data
 * @brief A structure to hold the BME680 sensor data.
 *
 * This structure holds the sensor data for temperature, pressure, humidity, IAQ, eCO2, and
 * breathVOC.
 */
struct bme680_data {
	/**
	 * @brief Temperature data in Celsius, ranging from -40 to 85.
	 */
	struct sensor_value temp;

	/**
	 * @brief Atmospheric pressure data in hPa, ranging from 300 to 1100. Sensitivity error is
	 * ±0.25%.
	 */
	struct sensor_value press;

	/**
	 * @brief Humidity data in percentage, ranging from 0 to 100%. Accuracy tolerance is ±3%.
	 */
	struct sensor_value humidity;

#if defined(CONFIG_BME68X_IAQ_EN)
	/**
	 * @brief IAQ Index data, ranging from 0 to 500. Sensor-to-sensor deviation is ±15%.
	 * For more details, see README.md.
	 */
	struct sensor_value iaq;

	/**
	 * @brief eCO2 data in ppm, ranging from 0 to infinity.
	 * For more details, see README.md.
	 */
	struct sensor_value eCO2;

	/**
	 * @brief Breath VOC data in ppm, ranging from 0 to 1000.
	 * For more details, see README.md.
	 */
	struct sensor_value breathVOC;
#endif // CONFIG_BME68X_IAQ_EN
};

/**
 * @brief A function for deep copying BME680 data, with Mutual Exclusion using a semaphore
 *
 * @return bme680 struct data
 */
struct bme680_data get_bme680_data(void);

#endif // CONFIG_BME68X
#endif // __APP_BME680_H__
