/**
 * @file switch.c
 * @brief
 * @author bradkim06
 * @version v0.01
 * @date 2023-09-18
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "enum_macro.h"
#include "switch.h"

LOG_MODULE_REGISTER(LOAD_SW);
DEFINE_ENUM(loadsw_dev, LOADSW_DEVICE)

#define SENSOR_EN   DT_ALIAS(loadsw0)
#define BATT_MON_EN DT_ALIAS(loadsw1)

#if !DT_NODE_EXISTS(SENSOR_EN) || !DT_NODE_EXISTS(BATT_MON_EN)
#error "Overlay for power output node not properly defined."
#endif

static const struct gpio_dt_spec switchs_arr[] = {
	GPIO_DT_SPEC_GET_OR(SENSOR_EN, gpios, {0}),
	GPIO_DT_SPEC_GET_OR(BATT_MON_EN, gpios, {0}),
};

static int switch_setup(void)
{
	int err;

	for (size_t i = 0U; i < ARRAY_SIZE(switchs_arr); i++) {
		if (!gpio_is_ready_dt(&switchs_arr[i])) {
			LOG_ERR("The load switch pin GPIO port is not ready.");
			return -ENODEV;
		}

		LOG_INF("Initializing pin with active level.");

		err = gpio_pin_configure_dt(&switchs_arr[i], GPIO_OUTPUT_ACTIVE);
		if (err != 0) {
			LOG_ERR("Configuring GPIO pin failed: %d", err);
			return err;
		}
	}

	return 0;
}

int switch_ctrl(enum loadsw_dev sw, bool power, bool wait)
{
	int err = gpio_pin_set_dt(&switchs_arr[sw], power);

	if (err != 0) {
		LOG_ERR("Setting Switch GPIO pin level failed: %d", err);
		return err;
	}

	if (wait) {
		LOG_INF("Turn %s %s, Waiting one second", (power ? "On" : "Off"), enum_to_str(sw));
		k_sleep(K_SECONDS(1));
	}

	LOG_INF("%s switching Finished", enum_to_str(sw));

	return 0;
}

SYS_INIT(switch_setup, APPLICATION, CONFIG_GPIO_INIT_PRIORITY);
