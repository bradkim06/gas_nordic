/**
 * @file src/led.c - led mosfet gpio control
 *
 * @brief Code for indicating the battery status with an LED.
 *
 * The LED operates every LED_THREAD_SLEEP_INTERVAL to conserve power, staying
 on for LED_TIME_MS.
 * Additionally, for low-power operation, it operates at a brightness of
 LED_PWM_LEVEL.
 * (If the battery status is higher than LOW_BATT_THRESHOLD, it lights up in
 green;
 * otherwise, it lights up in yellow).
 *
 * @author bradkim06@gmail.com
 */
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "battery.h"
#include "hhs_util.h"

/* Register the LED module with the application log level */
LOG_MODULE_REGISTER(LED, CONFIG_APP_LOG_LEVEL);

/* The battery thread's running period */
#define LED_THREAD_SLEEP_INTERVAL 10
/* LED on time */
#define LED_TIME_MS 50
/* LED Brightness level */
#define LED_PWM_LEVEL 100

/* access the Devicetree for the pwm_led node */
#define LED_PWM_NODE_ID DT_COMPAT_GET_ANY_STATUS_OKAY(pwm_leds)
static const struct device *led_pwm_device = DEVICE_DT_GET(LED_PWM_NODE_ID);

/* Define the LED device */
#define LED_DEVICE(X)                                                          \
    /* battery level stable */                                                 \
    X(LED_STATE_STABLE_BATTERY, = 0)                                           \
    /* battery level low */                                                    \
    X(LED_STATE_LOW_BATTERY, )
DECLARE_ENUM(led_device_state, LED_DEVICE)

/**
 * @brief Activate the LED MOSFET for a specified time.
 *
 * This function sets the brightness level of the LED, waits for a
 * specified time, and then turns the LED off.
 *
 * @param color: Selected LEDs are controlled by the enum led_dev.
 *
 * @return 0 if successful, error code otherwise.
 */
static int control_led(uint8_t led_color) {
    /* Set the LED brightness level */
    int err = led_set_brightness(led_pwm_device, led_color, LED_PWM_LEVEL);
    if (err < 0) {
        LOG_ERR("Error Code=%d, Brightness Level=%d\n", err, LED_PWM_LEVEL);
        return err;
    }
    /* Wait for a specified time before turning LED off */
    k_sleep(K_MSEC(LED_TIME_MS));

    /* Turn LED off */
    led_off(led_pwm_device, led_color);

    return 0;
}

/**
 * @brief LED thread function that illuminates the LED based on the battery
 * status at the interval of LED_THREAD_SLEEP_INTERVAL.
 */
static void led_thread_fn(void) {
    /* Check if the LED device is ready */
    if (!device_is_ready(led_pwm_device)) {
        LOG_ERR("Device %s is not ready", led_pwm_device->name);
        return;
    }

    while (1) {
        /* Get the battery percentage */
        // enum led_device_state led_color = (get_battery_percent().val1 >= 20)
        // 					  ? LED_STATE_STABLE_BATTERY
        // 					  : LED_STATE_LOW_BATTERY;

        enum led_device_state led_color = LED_STATE_STABLE_BATTERY;

        /* Activate the LED with the appropriate color based on the battery
         * status */
        control_led((uint32_t)led_color);
        /* Wait for LED_THREAD_SLEEP_INTERVAL before checking battery status */
        k_sleep(K_SECONDS(LED_THREAD_SLEEP_INTERVAL));
    }
}

/* Define the stack size and priority for the LED thread */
#define STACK_SIZE 1024
#define PRIORITY 6
/* Define the thread for the LED */
K_THREAD_DEFINE(led_id, STACK_SIZE, led_thread_fn, NULL, NULL, NULL, PRIORITY,
                0, 0);
