/**
 * @file gas.c
 * @brief
 * @author bradkim06
 * @version v0.01
 * @date 2023-09-18
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#include "enum_macro.h"
#include "bluetooth.h"
#include "hhs_math.h"
#include "gas.h"

LOG_MODULE_REGISTER(GAS_MON, CONFIG_ADC_LOG_LEVEL);

static struct gas_sensor_value curr_result[2];

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

DEFINE_ENUM(gas_device, DEVICE_LIST)

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

#define CHANGE_GAS_RESULT(curr, prev) ((curr.val1 != prev.val1) || (curr.val2 != prev.val2))

/** A discharge curve specific to the gas source. */
static const struct level_point levels[] = {
	// Maximum Overload 30% Oxygen
	{3000, 535},
	// Zero current (offset) <0.6 % vol O2
	{60, 0},
};

/* Data of ADC io-channels specified in devicetree. */
static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels, DT_SPEC_AND_COMMA)};

void gas_mon(void)
{
	int err;
	uint32_t count = 0;
	uint16_t buf;
	struct adc_sequence sequence = {
		.buffer = &buf,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(buf),
	};

	/* Configure channels individually prior to sampling. */
	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		if (!device_is_ready(adc_channels[i].dev)) {
			LOG_ERR("ADC controller device %s not ready", adc_channels[i].dev->name);
			return;
		}

		err = adc_channel_setup_dt(&adc_channels[i]);
		if (err < 0) {
			LOG_ERR("Could not setup channel #%d (%d)", i, err);
			return;
		}
	}

	// gas stable time delay
	k_sleep(K_SECONDS(1));
	static struct gas_sensor_value prev_pptt[2];
	moving_average_t *gas[2];
	for (int i = 0; i < 2; i++) {
		gas[i] = allocate_moving_average(10);
	}

	while (1) {

		LOG_DBG("ADC reading[%u]:", count++);
		for (size_t i = 0U; i < ARRAY_SIZE(adc_channels) - 1; i++) {
			int32_t val_mv;

			(void)adc_sequence_init_dt(&adc_channels[i], &sequence);

			err = adc_read(adc_channels[i].dev, &sequence);
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

			unsigned int gas_avg_pptt =
				(unsigned int)movingAvg(gas[i], (int)level_pptt(val_mv, levels));

			unsigned int remain_pptt = gas_avg_pptt % 10;
			gas_avg_pptt -= remain_pptt;
			gas_avg_pptt += (remain_pptt >= 5) ? 10 : 0;
			gas_avg_pptt /= 10;

			LOG_INF("%s - channel %d: "
				"%" PRId32 " mV %u",
				enum_to_str(i), adc_channels[i].channel_id, val_mv, gas_avg_pptt);

			curr_result[i].val1 = gas_avg_pptt / 10;
			curr_result[i].val2 = gas_avg_pptt % 10;
		}

		if (CHANGE_GAS_RESULT(curr_result[O2], prev_pptt[O2])) {
			// ble transmit
			LOG_WRN("gas value change");
			k_event_post(&bt_event, GAS_VAL_CHANGE);
			memcpy(prev_pptt, curr_result, sizeof(curr_result));
		}

		k_sleep(K_SECONDS(10));
	}
}

struct gas_sensor_value get_gas_value(enum gas_device dev)
{
	return curr_result[dev];
}

K_THREAD_DEFINE(gas_id, STACKSIZE, gas_mon, NULL, NULL, NULL, PRIORITY, 0, 0);
