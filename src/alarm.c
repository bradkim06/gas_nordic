#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/counter.h>

#include "bluetooth.h"

LOG_MODULE_REGISTER(ALARM);

/* size of stack area used by each thread */
#define STACKSIZE 1024

/* scheduling priority used by each thread */
#define PRIORITY 7

/* delay(us) */
#define DELAY		 1000 * 1000 * 60
#define ALARM_CHANNEL_ID 0

struct counter_alarm_cfg alarm_cfg;

#define TIMER DT_ALIAS(rtc)

#if !DT_NODE_EXISTS(TIMER)
#error "Overlay for timer node not properly defined."
#endif

static void test_counter_interrupt_fn(const struct device *counter_dev, uint8_t chan_id,
				      uint32_t ticks, void *user_data)
{
	LOG_WRN("!!! Alarm !!!");
	k_sem_give(&bt_sem);

	int err = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID, user_data);
	if (err != 0) {
		LOG_ERR("Alarm could not be set");
	}
}

int alarm_setup(void)
{
	const struct device *const counter_dev = DEVICE_DT_GET(TIMER);
	int err;

	LOG_INF("Counter alarm sample");

	if (!device_is_ready(counter_dev)) {
		LOG_INF("device not ready.");
		return -ENODEV;
	}

	counter_start(counter_dev);

	alarm_cfg.flags = 0;
	alarm_cfg.ticks = counter_us_to_ticks(counter_dev, DELAY);
	alarm_cfg.callback = test_counter_interrupt_fn;
	alarm_cfg.user_data = &alarm_cfg;

	err = counter_set_channel_alarm(counter_dev, ALARM_CHANNEL_ID, &alarm_cfg);
	LOG_INF("Set alarm in %u sec (%u ticks)",
		(uint32_t)(counter_ticks_to_us(counter_dev, alarm_cfg.ticks) / USEC_PER_SEC),
		alarm_cfg.ticks);

	if (-EINVAL == err) {
		LOG_ERR("Alarm settings invalid");
	} else if (-ENOTSUP == err) {
		LOG_ERR("Alarm setting request not supported");
	} else if (err != 0) {
		LOG_ERR("Error");
	}

	return err;
}

SYS_INIT(alarm_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
