/**
 * @file led.c
 * @brief
 * @author bradkim06
 * @version v0.01
 * @date 2023-09-18
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "led.h"

LOG_MODULE_REGISTER(LED, CONFIG_BOARD_HHS_LOG_LEVEL);
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
			LOG_ERR("The GPIO port is not ready.");
			return -ENODEV;
		}

		LOG_DBG("Initializing pin with inactive level.");

		// zephyr os에서 output port&pin의 gpio_pin_get()을 하려면 GPIO_INPUT을
		// 넣어야함.
		err = gpio_pin_configure_dt(&led_arr[i], GPIO_INPUT | GPIO_OUTPUT_INACTIVE);
		if (err != 0) {
			LOG_ERR("Configuring GPIO pin failed: %d", err);
			return err;
		}
	}

	return 0;
}

static int led_ctrl(enum led_dev color, bool power)
{
	if (gpio_pin_get_dt(&led_arr[color]) == power) {
		return 0;
	}

	int err = gpio_pin_set_dt(&led_arr[color], power);
	if (err != 0) {
		LOG_ERR("Setting LED GPIO pin level failed: %d", err);
		return err;
	}

	LOG_INF("Turn %s LED %s", (power ? "On" : "Off"), enum_to_str(color));

	return 0;
}

void batt_status_led(bool is_low_batt)
{
	if (is_low_batt) {
		led_ctrl(stablebatt_g, false);
		led_ctrl(lowbatt_y, true);
	} else {
		led_ctrl(stablebatt_g, true);
		led_ctrl(lowbatt_y, false);
	}
}

SYS_INIT(led_setup, APPLICATION, CONFIG_GPIO_INIT_PRIORITY);
