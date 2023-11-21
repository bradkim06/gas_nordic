/**
 * @file src/battery.c - Battery monitoring and management application code
 *
 * @brief This program provides functionality for battery monitoring and management.
 * It reads the battery voltage using an ADC and calculates battery levels and status.
 * It maintains a moving average for battery voltage and checks for low battery warnings.
 *
 * @author bradkim06@gmail.com
 */
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "battery.h"
#include "hhs_math.h"
#include "hhs_util.h"

LOG_MODULE_REGISTER(BATTERY, CONFIG_APP_LOG_LEVEL);

/* Devicetree Access */
#define VBATT DT_PATH(vbatt)

/* Used for Mutual Exclusion of battery data. */
K_SEM_DEFINE(batt_data_sem, 1, 1);

static struct battery_value batt_percent;

static bool battery_ok;

/** A discharge curve specific to the power source. */
static const struct level_point levels[] = {
	/* "Curve" here eyeballed from captured data for the [Adafruit
	 * 3.7v 2000 mAh](https://www.adafruit.com/product/2011) LIPO
	 * under full load that started with a charge of 3.96 V and
	 * dropped about linearly to 3.58 V over 15 hours.  It then
	 * dropped rapidly to 3.10 V over one hour, at which point it
	 * stopped transmitting.
	 *
	 * Based on eyeball comparisons we'll say that 15/16 of life
	 * goes between 3.95 and 3.55 V, and 1/16 goes between 3.55 V
	 * and 3.1 V.
	 */

	// dtp-102535 800mAh Batt
	// {10000, 4000},
	// tw-403030 300mAh Batt
	{10000, 3900},
	{625, 3550},
	{0, 3000},
};

struct io_channel_config {
	uint8_t channel;
};

/**
 * @struct divider_config
 * @brief This structure is used for configuring the divider.
 */
struct divider_config {
	/**< IO channel configuration */
	struct io_channel_config io_channel;
	/**< GPIO specification for power battery enable*/
	struct gpio_dt_spec power_gpios;
	/**
	 * @brief Output resistance in ohms.
	 *
	 * This is used as a flag value: if it is nonzero then
	 * the battery is measured through a voltage divider;
	 * otherwise it is assumed to be directly connected to Vdd.
	 */
	uint32_t output_ohm;
	/**< Full resistance in ohms */
	uint32_t full_ohm;
};

static const struct divider_config divider_config = {
	.io_channel =
		{
			DT_IO_CHANNELS_INPUT(VBATT),
		},
	.power_gpios = GPIO_DT_SPEC_GET_OR(VBATT, power_gpios, {}),
	.output_ohm = DT_PROP(VBATT, output_ohms),
	.full_ohm = DT_PROP(VBATT, full_ohms),
};

struct divider_data {
	const struct device *adc;
	struct adc_channel_cfg adc_cfg;
	struct adc_sequence adc_seq;
	int16_t raw;
};

static struct divider_data divider_data = {
	.adc = DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(VBATT)),
};

/**
 * @brief Setup the divider functionality.
 *
 * This function configures the divider based on the provided settings and hardware components.
 * It initializes the ADC sequence, sets up GPIO control for power management, and configures the
 * ADC channel. The function returns error codes if there are any issues during setup.
 *
 * @return 0 on success, or a negative error code on failure.
 */
static int divider_setup(void)
{
	const struct divider_config *cfg = &divider_config;
	const struct io_channel_config *iocp = &cfg->io_channel;
	const struct gpio_dt_spec *gcp = &cfg->power_gpios;
	struct divider_data *ddp = &divider_data;
	struct adc_sequence *asp = &ddp->adc_seq;
	struct adc_channel_cfg *accp = &ddp->adc_cfg;
	int rc;

	if (!device_is_ready(ddp->adc)) {
		LOG_ERR("ADC device is not ready %s", ddp->adc->name);
		return -ENOENT;
	}

	if (gcp->port) {
		if (!device_is_ready(gcp->port)) {
			LOG_ERR("%s: device not ready", gcp->port->name);
			return -ENOENT;
		}
		rc = gpio_pin_configure_dt(gcp, GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			LOG_ERR("Failed to control feed %s.%u: %d", gcp->port->name, gcp->pin, rc);
			return rc;
		}
	}

	*asp = (struct adc_sequence){
		.channels = BIT(0),
		.buffer = &ddp->raw,
		.buffer_size = sizeof(ddp->raw),
		.oversampling = 8,
		.calibrate = true,
	};

#ifdef CONFIG_ADC_NRFX_SAADC
#define BATTERY_ADC_GAIN ADC_GAIN_1
	*accp = (struct adc_channel_cfg){
		.gain = BATTERY_ADC_GAIN,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
	};

	if (cfg->output_ohm != 0) {
		accp->input_positive = SAADC_CH_PSELP_PSELP_AnalogInput0 + iocp->channel;
	} else {
		accp->input_positive = SAADC_CH_PSELP_PSELP_VDD;
	}

	asp->resolution = 14;
#else /* CONFIG_ADC_var */
#error Unsupported ADC
#endif /* CONFIG_ADC_var */

	rc = adc_channel_setup(ddp->adc, accp);
	LOG_DBG("Setup AIN%u got %d(%s)", iocp->channel, rc, (rc ? "err" : "none err"));

	return rc;
}

/**
 * @brief Setup the battery management.
 *
 * This function sets up the battery management by calling the "divider_setup" function.
 * It also checks if the battery setup was successful and updates the "battery_ok" flag accordingly.
 *
 * @return 0 on success, or a negative error code on failure.
 */
static int battery_setup(void)
{
	int rc = divider_setup();

	battery_ok = (rc == 0);
	LOG_DBG("Battery setup: %d(%s) %d(%s)", rc, (rc ? "err" : "none err"), battery_ok,
		(battery_ok ? "ok" : "fail"));

	return rc;
}

/** Enable or disable measurement of the battery voltage.
 *
 * @param enable true to enable, false to disable
 *
 * @return zero on success, or a negative error code.
 */
static int battery_measure_enable(bool enable)
{
	int rc = -ENOENT;

	if (battery_ok) {
		const struct gpio_dt_spec *gcp = &divider_config.power_gpios;

		rc = 0;
		if (gcp->port) {
			rc = gpio_pin_set_dt(gcp, enable);
		}
	}
	return rc;
}

/** Measure the battery voltage.
 *
 * @return the battery voltage in millivolts, or a negative error
 * code.
 */
static int battery_sample(void)
{
	int rc = -ENOENT;

	if (battery_ok) {
		struct divider_data *ddp = &divider_data;
		const struct divider_config *dcp = &divider_config;
		struct adc_sequence *sp = &ddp->adc_seq;

		rc = adc_read(ddp->adc, sp);
		sp->calibrate = false;
		if (rc == 0) {
			int32_t val = ddp->raw;

			adc_raw_to_millivolts(adc_ref_internal(ddp->adc), ddp->adc_cfg.gain,
					      sp->resolution, &val);

			if (dcp->output_ohm != 0) {
				rc = val * (uint64_t)dcp->full_ohm / dcp->output_ohm;
				// LOG_DBG("raw %u ~ %u mV => %d mV", ddp->raw, val, rc);
			} else {
				rc = val;
				// LOG_DBG("raw %u ~ %u mV", ddp->raw, val);
			}
		}
	}

	return (rc < 0) ? 0 : rc;
}

/**
 * @brief Measure battery status and update battery level information.
 *
 * This function measures the battery status, calculates the battery level in pptt, and updates the
 * battery level information. It also checks if the battery level is below a low battery threshold
 * and logs a warning if necessary.
 *
 * @return True if the battery level is below the low battery threshold, false otherwise.
 */
static bool measure_battery_status(moving_average_t *battery_status)
{
	// Get current battery voltage
	int current_battery_mV = battery_sample();
	// Calculate moving average
	int average_battery_mV = calculate_moving_average(battery_status, current_battery_mV);
	// Calculate power-to-time-to-charge ratio
	unsigned int pptt = calculate_level_pptt(average_battery_mV, levels);

	// Take the battery semaphore to ensure exclusive access to the global battery pptt value
	k_sem_take(&batt_data_sem, K_FOREVER);
	batt_percent.val1 = pptt / 100;        // Update the first digit of the pptt value
	batt_percent.val2 = (pptt % 100) / 10; // Update the second digit of the pptt value
	k_sem_give(&batt_data_sem);            // Release the battery semaphore

	// Check if the pptt is below the low battery threshold and set the low battery status
	// accordingly
	bool is_low_battery = (pptt < LOW_BATT_THRESHOLD) ? true : false;

	// Define a string for logging purposes
#define MAX_LOG_CASE "low batt warning curr : -2147483648mV avg : -2147483648mV; -2147483648pptt, "
	char log_message[sizeof(MAX_LOG_CASE)];
	// Format the string with the current battery voltage, moving average, and pptt values
	sprintf(log_message, "curr : %dmV avg : %d mV; %u pptt, ", current_battery_mV,
		average_battery_mV, pptt);
	// Log the appropriate message based on the low battery status
	CODE_IF_ELSE(is_low_battery, LOG_INF("low batt warning %s", log_message),
		     LOG_DBG("stable batt %s", log_message));

	return is_low_battery; // Return the low battery status
}

/**
 * @brief Battery measurement thread function.
 *
 * This function is the entry point for the battery measurement thread. It initializes the
 * battery measurement and then periodically calls the "measure_battery_status" function to measure
 * battery status. The battery status is maintained with a moving average, and low battery
 * warnings are logged as needed.
 */
static void battery_measurement_thread(void)
{
/* Define filter size for moving average */
#define FILTER_SIZE 30

	/* Allocate memory for moving average */
	/* battery percent moving average filter */
	moving_average_t *battery_status = allocate_moving_average(FILTER_SIZE);
	if (battery_status == NULL) {
		return;
	}

	/* Enable battery measurement */
	int measurement_status = battery_measure_enable(true);

	/* Check for errors */
	if (measurement_status != 0) {
		/* Log error and return */
		LOG_ERR("Failed to initialize battery measurement: %d", measurement_status);
		free_moving_average(&battery_status);
		return;
	}

/* Define initial delay for thread in seconds */
#define INITIAL_DELAY_SEC 3

	/* Sleep for initial delay */
	k_sleep(K_SECONDS(INITIAL_DELAY_SEC));

	/* Loop for continuous measurement */
	while (1) {
		/* Call function for measuring battery */
		measure_battery_status(battery_status);

/* Define thread period in seconds */
#define THREAD_PERIOD_SEC 2

		/* Sleep for thread period */
		k_sleep(K_SECONDS(THREAD_PERIOD_SEC));
	}
}

struct battery_value get_battery_percent(void)
{
	k_sem_take(&batt_data_sem, K_FOREVER);
	struct battery_value copy = batt_percent;
	k_sem_give(&batt_data_sem);

	return copy;
}

SYS_INIT(battery_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#define STACKSIZE 1024
#define PRIORITY  3
K_THREAD_DEFINE(battmon_id, STACKSIZE, battery_measurement_thread, NULL, NULL, NULL, PRIORITY, 0,
		0);
