/**
 * @file src/gas.c - electrochemical gas sensor adc application code
 *
 * @brief Program for Reading electrochemical Gas Sensor ADC Information Using Nordic's SAADC.
 *
 * @author bradkim06@gmail.com
 */
#include <math.h>
#include <stdlib.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bluetooth.h"
#include "gas.h"
#include "hhs_math.h"
#include "hhs_util.h"
#include "bme680_app.h"

LOG_MODULE_REGISTER(GAS_MON, CONFIG_APP_LOG_LEVEL);

DEFINE_ENUM(gas_device, DEVICE_LIST)
/* Used for Mutual Exclusion of gas sensor data. */
K_SEM_DEFINE(gas_sem, 1, 1);
/* The current value of the gas sensor */
static struct gas_sensor_value curr_result[2];

/* A discharge curve specific to the gas source. */
static const struct level_point levels[] = {
	// Measurement Range Max 25% Oxygen
	{250, 662},
	// Zero current (offset) <0.6 % vol O2
	{0, 0},
};

/* A discharge curve specific to the gas temperature coefficeint. */
static const struct level_point coeff_levels[] = {
	/* Output Temperature Coefficeint Oxygen Sensor */
	{10500, 5000}, {10400, 4000}, {10000, 2000}, {9600, 0}, {9000, -2000},
};

/* pwm_led Devicetree access */
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif
#define DT_SPEC_AND_COMMA(node_id, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* Data of ADC io-channels specified in devicetree. */
static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)};
/* Gas sensor data moving average filter */
static moving_average_t *gas[2];

/**
 * @brief Function to perform gas sensor measurements.
 *
 * This function reads gas sensor values using the ADC interface, applies temperature
 * compensation, and calculates moving averages. It also checks for any changes in
 * gas sensor values and posts an event accordingly.
 */
static void measuring(void)
{
	uint16_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(buf),
	};

	static int prev_o2 = 0;
	bool o2_changed = false;

	// for (size_t i = 0U; i < ARRAY_SIZE(adc_channels) - 1; i++) {
	for (size_t i = 0U; i < 1; i++) {
		int32_t val_mv;

		(void)adc_sequence_init_dt(&adc_channels[i], &sequence);

		int err = adc_read(adc_channels[i].dev, &sequence);
		if (err < 0) {
			LOG_WRN("Could not read (%d)", err);
			continue;
		}

		/*
		 * If using differential mode, the 16 bit value
		 * in the ADC sample buffer should be a signed 2's
		 * complement value.
		 */
		if (adc_channels[i].channel_cfg.differential) {
			val_mv = (int32_t)((int16_t)buf);
		} else {
			val_mv = (buf < 0) ? 0 : (int32_t)buf;
		}
		err = adc_raw_to_millivolts_dt(&adc_channels[i], &val_mv);
		/* conversion to mV may not be supported, skip if not */
		if (err < 0) {
			LOG_WRN(" (value in mV not available)");
			continue;
		}

		struct bme680_data env = get_bme680_data();
		double temp_coeff =
			(10000.f /
			 (double)level_pptt(env.temp.val1 * 100 + env.temp.val2, coeff_levels));
		int32_t calib_val_mv = (int32_t)round(val_mv * temp_coeff);
		LOG_DBG("temp coeff : %f raw : %d calib : %d", temp_coeff, val_mv, calib_val_mv);

		int32_t avg_mv = movingAvg(gas[i], calib_val_mv);
		int gas_avg_pptt = level_pptt(avg_mv, levels);

/* Gas sensor threshold for triggering BLE notify events on change */
#define O2_THRES 2
		if (abs(gas_avg_pptt - prev_o2) > O2_THRES) {
			o2_changed = true;
			prev_o2 = gas_avg_pptt;
		}

		k_sem_take(&gas_sem, K_FOREVER);
		curr_result[i].val1 = gas_avg_pptt / 10;
		curr_result[i].val2 = gas_avg_pptt % 10;
		k_sem_give(&gas_sem);

		LOG_DBG("%s - channel %d: "
			" curr %" PRId32 "mV avg %" PRId32 "mV %d.%d%%",
			enum_to_str(i), adc_channels[i].channel_id, calib_val_mv, avg_mv,
			curr_result[i].val1, curr_result[i].val2);
	}

	if (o2_changed) {
		LOG_INF("value changed %d.%d%%", curr_result[0].val1, curr_result[0].val2);
		k_event_post(&bt_event, GAS_VAL_CHANGE);
	}
}

/**
 * @brief Gas sensor thread function.
 *
 * This function configures ADC channels, sets up moving averages, and performs gas sensor
 * measurements. It reads and processes gas sensor data based on temperature compensation and checks
 * for changes.
 */
#define FILTER_SIZE 30
static void gas_thread_fn(void)
{
	/* Configure channels individually prior to sampling. */
	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		if (!device_is_ready(adc_channels[i].dev)) {
			LOG_ERR("ADC controller device %s not ready", adc_channels[i].dev->name);
			return;
		}

		int err = adc_channel_setup_dt(&adc_channels[i]);
		if (err < 0) {
			LOG_ERR("Could not setup channel #%d (%d)", i, err);
			return;
		}
	}

	for (int i = 0; i < 2; i++) {
		gas[i] = allocate_moving_average(FILTER_SIZE);
	}

	if (k_sem_take(&temp_sem, K_SECONDS(10)) != 0) {
		LOG_WRN("Temperature Input data not available!");
	} else {
		/* fetch available data */
		LOG_INF("temperature sensing ok");
	}

#define THREAD_PERIOD_SEC 2
	while (1) {
		measuring();
		k_sleep(K_SECONDS(THREAD_PERIOD_SEC));
	}
}

struct gas_sensor_value get_gas_data(enum gas_device dev)
{
	k_sem_take(&gas_sem, K_FOREVER);
	struct gas_sensor_value copy = curr_result[dev];
	k_sem_give(&gas_sem);

	return copy;
}

#define STACKSIZE 1024
#define PRIORITY  8
K_THREAD_DEFINE(gas_id, STACKSIZE, gas_thread_fn, NULL, NULL, NULL, PRIORITY, 0, 0);
