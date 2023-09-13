#include "switch.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(LOAD_SW, CONFIG_GPIO_LOG_LEVEL);

static const char *SW_STRING[] = {
	"BME680_SENSOR_EN",
	"BATT_MON_EN",
	"LOW_BATT_INDICATOR",
};

#define SENSOR_EN	  DT_NODELABEL(sw0)
#define BATT_MON_EN	  DT_NODELABEL(sw1)
#define LOWBATT_INDICATOR DT_NODELABEL(sw2)

#if !DT_NODE_EXISTS(SENSOR_EN) || !DT_NODE_EXISTS(BATT_MON_EN) || !DT_NODE_EXISTS(LOWBATT_INDICATOR)
#error "Overlay for power output node not properly defined."
#endif

static const struct gpio_dt_spec switchs_arr[] = {
	GPIO_DT_SPEC_GET_OR(SENSOR_EN, gpios, {0}),
	GPIO_DT_SPEC_GET_OR(BATT_MON_EN, gpios, {0}),
	GPIO_DT_SPEC_GET_OR(LOWBATT_INDICATOR, gpios, {0}),
};

static int switch_setup(void)
{
	int err;

	for (size_t i = 0U; i < ARRAY_SIZE(switchs_arr); i++) {
		if (!gpio_is_ready_dt(&switchs_arr[i])) {
			LOG_ERR("The load switch pin GPIO port is not ready.");
			return -ENODEV;
		}

		LOG_INF("Initializing pin with inactive level.");

		err = gpio_pin_configure_dt(&switchs_arr[i], GPIO_OUTPUT_INACTIVE);
		if (err != 0) {
			LOG_ERR("Configuring GPIO pin failed: %d", err);
			return err;
		}

		LOG_INF("load switch Waiting one second.");
	}

	return 0;
}

int switch_ctrl(enum load_switch index, bool power)
{
	int err = gpio_pin_set_dt(&switchs_arr[index], power);

	if (err != 0) {
		LOG_ERR("Setting GPIO pin level failed: %d", err);
		return err;
	}

	LOG_DBG("Turn on %s, Waiting one second", SW_STRING[index]);
	k_sleep(K_MSEC(1000));
	LOG_DBG("%s switching Finished", SW_STRING[index]);

	return 0;
}

SYS_INIT(switch_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
