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

#include "battery.h"
#include "led.h"

LOG_MODULE_REGISTER(LED, CONFIG_BOARD_HHS_LOG_LEVEL);
DEFINE_ENUM(led_dev, LED_DEVICE);

#define LED_STACK_SIZE 512
#define LED_PRIORITY   11

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

static int led_ctrl(enum led_dev color)
{
	int err = gpio_pin_set_dt(&led_arr[color], true);
	if (err != 0) {
		LOG_ERR("Setting LED GPIO pin level failed: %d", err);
		return err;
	}

	LOG_INF("Turn On LED %s", enum_to_str(color));
	k_sleep(K_MSEC(100));
	LOG_INF("Turn Off LED %s", enum_to_str(color));

	err = gpio_pin_set_dt(&led_arr[color], false);
	if (err != 0) {
		LOG_ERR("Setting LED GPIO pin level failed: %d", err);
		return err;
	}

	return 0;
}

static void batt_status_led(void)
{
	struct batt_value batt = {0};
    k_sleep(K_SECONDS(2));

	while (1) {
		batt = get_batt_percent();
		enum led_dev color = (batt.val1 >= 20) ? stablebatt_g : lowbatt_y;

		led_ctrl(color);
		k_sleep(K_SECONDS(5));
	}
}

SYS_INIT(led_setup, APPLICATION, CONFIG_GPIO_INIT_PRIORITY);
K_THREAD_DEFINE(led_id, LED_STACK_SIZE, batt_status_led, NULL, NULL, NULL, LED_PRIORITY, 0, 0);
