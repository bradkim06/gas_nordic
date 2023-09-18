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

LOG_MODULE_REGISTER(GAS_MON, CONFIG_ADC_LOG_LEVEL);

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

#define DEVICE_LIST(X)                                                                             \
	X(O2, = 0)                                                                                 \
	X(GAS, )

CREATE_ENUM(gas_device, DEVICE_LIST);

#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || !DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

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
	k_sleep(K_SECONDS(10));

	while (1) {
		LOG_INF("ADC reading[%u]:", count++);
		for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
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

			LOG_INF("%s - channel %d: "
				"%" PRId32 " = %" PRId32 " mV",
				enum_to_str(i), adc_channels[i].channel_id, val_mv, val_mv);
		}

		k_sleep(K_MSEC(10000));
	}
}

K_THREAD_DEFINE(gas_id, STACKSIZE, gas_mon, NULL, NULL, NULL, PRIORITY, 0, 0);
