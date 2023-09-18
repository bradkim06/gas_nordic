/**
 * @file led.c
 * @brief
 * @author bradkim06
 * @version v0.01
 * @date 2023-09-18
 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "led.h"

LOG_MODULE_REGISTER(LED, CONFIG_GPIO_LOG_LEVEL);
DEFINE_ENUM(led_dev, LED_DEVICE);

/* The devicetree node identifier for the "led0" alias. */
#define RUNNING_STATUS	  DT_ALIAS(led0)
#define LOWBATT_INDICATOR DT_ALIAS(led1)

#if !DT_NODE_EXISTS(RUNNING_STATUS) || !DT_NODE_EXISTS(LOWBATT_INDICATOR)
#error "Overlay for power output node not properly defined."
#endif

static const struct gpio_dt_spec led_arr[] = {
	GPIO_DT_SPEC_GET(RUNNING_STATUS, gpios),
	GPIO_DT_SPEC_GET(LOWBATT_INDICATOR, gpios),
};

static int led_setup(void)
{
	int err;

	for (size_t i = 0U; i < ARRAY_SIZE(led_arr); i++) {
		if (!gpio_is_ready_dt(&led_arr[i])) {
			LOG_ERR("The load switch pin GPIO port is not ready.");
			return -ENODEV;
		}

		LOG_INF("Initializing pin with inactive level.");

		err = gpio_pin_configure_dt(&led_arr[i], GPIO_OUTPUT_INACTIVE);
		if (err != 0) {
			LOG_ERR("Configuring GPIO pin failed: %d", err);
			return err;
		}
	}

	return 0;
}

int led_ctrl(enum led_dev color, bool power)
{
	int err = gpio_pin_set_dt(&led_arr[color], power);

	if (err != 0) {
		LOG_ERR("Setting LED GPIO pin level failed: %d", err);
		return err;
	}

	LOG_INF("Turn %s LED %s", (power ? "On" : "Off"), enum_to_str(color));

	return 0;
}

SYS_INIT(led_setup, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
