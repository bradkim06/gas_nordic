/**
 * @file led.c
 * @brief
 * @author bradkim06
 * @version v0.10
 * @date 2023-09-18
 */
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <errno.h>
#include <zephyr/drivers/led.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "battery.h"
#include "hhs_util.h"

LOG_MODULE_REGISTER(LED, CONFIG_APP_LOG_LEVEL);

#define LED_TIME_MS   100
#define LED_PWM_LEVEL 10

#define LED_PWM_NODE_ID DT_COMPAT_GET_ANY_STATUS_OKAY(pwm_leds)
static const struct device *led_pwm = DEVICE_DT_GET(LED_PWM_NODE_ID);

#define LED_DEVICE(X)                                                                              \
	X(stablebatt_g, = 0)                                                                       \
	X(lowbatt_y, )
DECLARE_ENUM(led_dev, LED_DEVICE)

static int led_ctrl(uint8_t color, uint16_t delay)
{
	int err = led_set_brightness(led_pwm, color, LED_PWM_LEVEL);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, LED_PWM_LEVEL);
		return err;
	}
	k_sleep(K_MSEC(delay));

	/* Turn LED off. */
	led_off(led_pwm, color);

	return 0;
}

static void batt_status_led(void)
{
	if (!device_is_ready(led_pwm)) {
		LOG_ERR("Device %s is not ready", led_pwm->name);
		return;
	}

	while (1) {
		k_sleep(K_SECONDS(5));
		enum led_dev color = (get_batt_percent().val1 >= 20) ? stablebatt_g : lowbatt_y;

		led_ctrl((uint32_t)color, LED_TIME_MS);
	}
}

#define STACK_SIZE 1024
#define PRIORITY   11
K_THREAD_DEFINE(led_id, STACK_SIZE, batt_status_led, NULL, NULL, NULL, PRIORITY, 0, 0);
