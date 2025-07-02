/**
 * @file power_switch.c - system power control via GPIO button
 *
 * @brief Implements a simple power switch using P0.31. Holding the
 * button for around one second puts the system into system off
 * state and the same button is used to wake it again.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>

#include <hal/nrf_gpio.h>
#include <hal/nrf_power.h>

LOG_MODULE_REGISTER(POWER_SWITCH, CONFIG_APP_LOG_LEVEL);

static const struct gpio_dt_spec power_button = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, {0});

static struct gpio_callback button_cb;
static struct k_work_delayable power_work;
static bool sleeping;

static void power_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (gpio_pin_get_dt(&power_button) > 0) {
		sleeping = !sleeping;
		if (sleeping) {
#ifdef CONFIG_PM
			nrf_gpio_cfg_sense_set(power_button.pin, NRF_GPIO_PIN_SENSE_HIGH);
			pm_state_force(0, &(struct pm_state_info){PM_STATE_SOFT_OFF, 0, 0});
			nrf_power_system_off();
#endif
		}
	}
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_schedule(&power_work, K_SECONDS(1));
}

static int power_switch_init(void)
{
	if (!device_is_ready(power_button.port)) {
		return -ENODEV;
	}

	k_work_init_delayable(&power_work, power_work_handler);

	gpio_pin_configure_dt(&power_button, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&power_button, GPIO_INT_EDGE_TO_ACTIVE);

	gpio_init_callback(&button_cb, button_pressed, BIT(power_button.pin));
	gpio_add_callback(power_button.port, &button_cb);

	return 0;
}

SYS_INIT(power_switch_init, APPLICATION, 50);
