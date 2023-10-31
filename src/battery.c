/**
 * @file battery.c
 * @brief
 * @author bradkim06
 * @version v0.01
 * @date 2023-09-18
 */
#include <math.h>
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

static struct batt_value batt_pptt;

LOG_MODULE_REGISTER(BATTERY, CONFIG_APP_LOG_LEVEL);
K_SEM_DEFINE(batt_sem, 1, 1);

#define VBATT            DT_PATH(vbatt)
#define BATTERY_ADC_GAIN ADC_GAIN_1

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 9

static moving_average_t *batt;

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
	{0, 3100},
};

struct io_channel_config {
	uint8_t channel;
};

struct divider_config {
	struct io_channel_config io_channel;
	struct gpio_dt_spec power_gpios;
	/* output_ohm is used as a flag value: if it is nonzero then
	 * the battery is measured through a voltage divider;
	 * otherwise it is assumed to be directly connected to Vdd.
	 */
	uint32_t output_ohm;
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

static bool battery_ok;

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

static bool measuring(bool isInit)
{
	/* Burn battery so you can see that this works over time */
	int curr_batt_mV = battery_sample();
	int batt_mV = movingAvg(batt, curr_batt_mV);

	unsigned int pptt = level_pptt(batt_mV, levels);

	k_sem_take(&batt_sem, K_FOREVER);
	batt_pptt.val1 = pptt / 100;
	batt_pptt.val2 = (pptt % 100) / 10;
	k_sem_give(&batt_sem);

	bool low_batt_status = (pptt < LOW_BATT_THRESHOLD) ? true : false;
	if (!isInit) {
		char logStr[100] = {0};
		sprintf(logStr, "curr : %dmV avg : %d mV; %u pptt, ", curr_batt_mV, batt_mV, pptt);
		CODE_IF_ELSE(low_batt_status, LOG_INF("low batt warnning %s", logStr),
			     LOG_DBG("stable batt %s", logStr));
	}

	return low_batt_status;
}

void battmon(void)
{
	batt = allocate_moving_average(30);
	int rc = battery_measure_enable(true);

	if (rc != 0) {
		LOG_ERR("Failed initialize battery measurement: %d", rc);
		return;
	}

	k_sleep(K_SECONDS(3));

	while (1) {
		measuring(false);
		k_sleep(K_SECONDS(2));
	}
}

struct batt_value get_batt_percent(void)
{
	k_sem_take(&batt_sem, K_FOREVER);
	struct batt_value copy = batt_pptt;
	k_sem_give(&batt_sem);

	return copy;
}

SYS_INIT(battery_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
K_THREAD_DEFINE(battmon_id, STACKSIZE, battmon, NULL, NULL, NULL, PRIORITY, 0, 0);
