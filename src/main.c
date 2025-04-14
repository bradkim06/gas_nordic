/**
 * @file src/main.c - The starting point of the user application
 *
 * @brief The operation of the main() function, which is the first function called after kernel
 * initialization
 *
 * @author bradkim06@gmail.com
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

#include "gas.h"
#include "version.h"

LOG_MODULE_REGISTER(MAIN, CONFIG_APP_LOG_LEVEL);
FIRMWARE_INFO();

/**
 * @brief This thread performs kernel initialization, then calls the application’s main() function
(if one is defined).

By default, the main thread uses the highest configured preemptible thread priority (i.e. 0). If the
kernel is not configured to support preemptible threads, the main thread uses the lowest configured
cooperative thread priority (i.e. -1).

The main thread is an essential thread while it is performing kernel initialization or executing the
application’s main() function; this means a fatal system error is raised if the thread aborts. If
main() is not defined, or if it executes and then does a normal return, the main thread terminates
normally and no error is raised.
 *
 * @return must be 0(none error)
 */
int main(void)
{
	LOG_INF("Firmware Info : %s", firmware_info);
	LOG_INF("Board:%s SoC:%s Rom:%dkb Ram:%dkb", CONFIG_BOARD, CONFIG_SOC, CONFIG_FLASH_SIZE,
		CONFIG_SRAM_SIZE);

	return 0;
}

#if DT_HAS_COMPAT_STATUS_OKAY(nordic_nrf_wdt)
/* Maximum and minimum window for watchdog timer in milliseconds */
#define WDT_MAX_WINDOW 5000U
#define WDT_MIN_WINDOW 0U

/* Interval for feeding the watchdog timer(ms) */
#define WDT_FEED_INTERVAL 1000U

/* Option for watchdog timer */
#define WDT_OPER_MODE WDT_OPT_PAUSE_HALTED_BY_DBG
#endif

/**
 * @brief Watchdog thread function.
 *
 * This function sets up the watchdog timer and continuously feeds it.
 */
static void watchdog_thread_fn(void)
{
	int setup_error_status;  // More descriptive variable name
	int watchdog_channel_id; // More descriptive variable name
	const struct device *const watchdog_device =
		DEVICE_DT_GET(DT_ALIAS(watchdog0)); // More descriptive variable name

	// Check if the device is ready before proceeding
	if (!device_is_ready(watchdog_device)) {
		LOG_ERR("%s: device not ready.\n", watchdog_device->name);
		return;
	}

	struct wdt_timeout_cfg watchdog_configuration = {
		/* Reset SoC when watchdog timer expires. */
		.flags = WDT_FLAG_RESET_SOC,

		/* Expire watchdog after max window */
		.window.min = WDT_MIN_WINDOW,
		.window.max = WDT_MAX_WINDOW,
	};

	// Install timeout for the watchdog
	watchdog_channel_id = wdt_install_timeout(watchdog_device, &watchdog_configuration);
	if (watchdog_channel_id < 0) {
		LOG_ERR("Error installing watchdog timeout\n");
		return;
	}

	// Set up the watchdog
	setup_error_status = wdt_setup(watchdog_device, WDT_OPER_MODE);
	if (setup_error_status < 0) {
		printk("Error setting up watchdog\n");
		return;
	}

	// Feed the watchdog
	while (1) {
		wdt_feed(watchdog_device, watchdog_channel_id);
		k_sleep(K_MSEC(WDT_FEED_INTERVAL));
	}
}

#define STACKSIZE 1024
#define PRIORITY  14
K_THREAD_DEFINE(watchdog_thread_id, STACKSIZE, watchdog_thread_fn, NULL, NULL, NULL, PRIORITY, 0,
		0);
