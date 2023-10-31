/**
 * @file gas.c
 * @brief
 * @author bradkim06
 * @version v0.01
 * @date 2023-09-18
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

static struct gas_sensor_value curr_result[2];

DEFINE_ENUM(gas_device, DEVICE_LIST)

#define O2_THRES 2

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/** A discharge curve specific to the gas source. */
static const struct level_point levels[] = {
	// Measurement Range 25% Oxygen
	{250, 662},
	// Zero current (offset) <0.6 % vol O2
	{0, 0},
};

/** A discharge curve specific to the gas source. */
static const struct level_point coeff_levels[] = {
	// Output Temperature Coefficeint Oxygen
	{10500, 5000}, {10400, 4000}, {10000, 2000}, {9600, 0}, {9000, -2000},
};

/* Data of ADC io-channels specified in devicetree. */
static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)};
static moving_average_t *gas[2];

static void measuring(bool isInit)
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

		double temp_coeff =
			(10000.f / (double)level_pptt(bme680.temp.val1 * 100 + bme680.temp.val2,
						      coeff_levels));
		int32_t calib_val_mv = (int32_t)round(val_mv * temp_coeff);
		LOG_DBG("temp coeff : %f raw : %d calib : %d", temp_coeff, val_mv, calib_val_mv);

		int32_t avg_mv = movingAvg(gas[i], calib_val_mv);
		int gas_avg_pptt = level_pptt(avg_mv, levels);

		if (abs(gas_avg_pptt - prev_o2) > O2_THRES) {
			o2_changed = true;
			prev_o2 = gas_avg_pptt;
		}

		curr_result[i].val1 = gas_avg_pptt / 10;
		curr_result[i].val2 = gas_avg_pptt % 10;

		if (!isInit) {
			LOG_DBG("%s - channel %d: "
				" curr %" PRId32 "mV avg %" PRId32 "mV %d.%d%%",
				enum_to_str(i), adc_channels[i].channel_id, calib_val_mv, avg_mv,
				curr_result[i].val1, curr_result[i].val2);
		}
	}

	if (o2_changed) {
		if (!isInit) {
			LOG_INF("value changed %d.%d%%", curr_result[0].val1, curr_result[0].val2);
			k_event_post(&bt_event, GAS_VAL_CHANGE);
		}
	}
}

void gas_mon(void)
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
		gas[i] = allocate_moving_average(30);
	}

	if (k_sem_take(&temp_sem, K_SECONDS(10)) != 0) {
		LOG_WRN("Temperature Input data not available!");
	} else {
		/* fetch available data */
		LOG_INF("temperature sensing ok");
	}

	while (1) {
		measuring(false);
		k_sleep(K_SECONDS(2));
	}
}

struct gas_sensor_value get_gas_value(enum gas_device dev)
{
	return curr_result[dev];
}

/* size of stack area used by each thread */
#define STACKSIZE 1024
/* scheduling priority used by each thread */
#define PRIORITY  8
K_THREAD_DEFINE(gas_id, STACKSIZE, gas_mon, NULL, NULL, NULL, PRIORITY, 0, 0);
