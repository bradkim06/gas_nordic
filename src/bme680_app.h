#ifndef __APP_BME680_H__
#define __APP_BME680_H__

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

extern struct k_sem temp_sem;

/* average current consumption
 * 3.7 µA at 1 Hz humidity, pressure and temperature
 * 0.09‒12 mA for p/h/T/gas depending on operation mode
 */
#if defined(CONFIG_BME68X)
struct bme680_data {
	/* The unit of temperature data is Celsius,
	 * and the range is from -40 to 85. */
	struct sensor_value temp;
	/* The unit of atmospheric pressure is hPa,
	 * and the range is from 300 to 1100. (Sensitivity Err ±0.25%)*/
	struct sensor_value press;
	/* The unit of humidity is percentage,
	 * and the range is from 0 to 100%. (Accuracy tolerance ±3%)*/
	struct sensor_value humidity;
#if defined(CONFIG_BME68X_IAQ_EN)
	/* The unit of iaq is IAQ Index ,
	 * and the range is from 0 to 500 (Sensor-to-sensor deviation ±15%).
	 * See detail README.md */
	struct sensor_value iaq;
	/* The unit of co2 is ppm,
	 * and the range is from 0 to infinity.
	 * See detail README.md */
	struct sensor_value eCO2;
	/* The unit of voc is ppm,
	 * and the range is from 0 to 1000.
	 * See detail README.md */
	struct sensor_value breathVOC;
#endif
};

/**
 * @brief A function for deep copying BME680 data, with Mutual Exclusion using a semaphore
 *
 * @return bme680 struct data
 */
struct bme680_data get_bme680_data(void);

#endif
#endif
