/**
 * @file src/led.c - led mosfet gpio control
 *
 * @brief Code for indicating the battery status with an LED.
 *
 * The LED operates every LED_THREAD_SLEEP_SECOND to conserve power, staying on for LED_TIME_MS.
 * Additionally, for low-power operation, it operates at a brightness of LED_PWM_LEVEL.
 * (If the battery status is higher than LOW_BATT_THRESHOLD, it lights up in green;
 * otherwise, it lights up in yellow).
 *
 * @author bradkim06@gmail.com
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

/* The battery thread's running period */
#define LED_THREAD_SLEEP_SECOND 5
/* LED on time */
#define LED_TIME_MS             100
/* LED Brightness level */
#define LED_PWM_LEVEL           10

/* pwm_led Devicetree access */
#define LED_PWM_NODE_ID DT_COMPAT_GET_ANY_STATUS_OKAY(pwm_leds)
static const struct device *led_pwm = DEVICE_DT_GET(LED_PWM_NODE_ID);

#define LED_DEVICE(X)                                                                              \
	/* battery level stable */                                                                 \
	X(stablebatt_g, = 0)                                                                       \
	/* battery level low */                                                                    \
	X(lowbatt_y, )
DECLARE_ENUM(led_dev, LED_DEVICE)

/**
 * @brief Activate the LED MOSFET for LED_TIME_MS
 *
 * @param color: Selected LEDs are controlled by the enum led_dev.
 *
 * @return none error(0)
 */
static int led_ctrl(uint8_t color)
{
	int err = led_set_brightness(led_pwm, color, LED_PWM_LEVEL);
	if (err < 0) {
		LOG_ERR("err=%d brightness=%d\n", err, LED_PWM_LEVEL);
		return err;
	}
	k_sleep(K_MSEC(LED_TIME_MS));

	/* Turn LED off. */
	led_off(led_pwm, color);

	return 0;
}

/**
 * @brief LED thread function that illuminates the LED based on the battery status at the interval
 * of LED_THREAD_SLEEP_SECOND.
 */
static void led_thread_fn(void)
{
	if (!device_is_ready(led_pwm)) {
		LOG_ERR("Device %s is not ready", led_pwm->name);
		return;
	}

	while (1) {
		k_sleep(K_SECONDS(LED_THREAD_SLEEP_SECOND));
		enum led_dev color = (get_batt_percent().val1 >= 20) ? stablebatt_g : lowbatt_y;

		led_ctrl((uint32_t)color);
	}
}

#define STACK_SIZE 1024
#define PRIORITY   11
K_THREAD_DEFINE(led_id, STACK_SIZE, led_thread_fn, NULL, NULL, NULL, PRIORITY, 0, 0);
