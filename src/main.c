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
/* Nordic supports a callback, but it has 61.2 us to complete before
 * the reset occurs, which is too short for this sample to do anything
 * useful.  Explicitly disallow use of the callback.
 */
#define WDT_ALLOW_CALLBACK 0
#endif

#ifndef WDT_MAX_WINDOW
#define WDT_MAX_WINDOW 5000U
#endif

#ifndef WDT_MIN_WINDOW
#define WDT_MIN_WINDOW 0U
#endif

#ifndef WDG_FEED_INTERVAL
#define WDG_FEED_INTERVAL 1000U
#endif

#ifndef WDT_OPT
#define WDT_OPT WDT_OPT_PAUSE_HALTED_BY_DBG
#endif

static void watchdog_thread(void)
{
	int err;
	int wdt_channel_id;
	const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));

	printk("Watchdog sample application\n");

	if (!device_is_ready(wdt)) {
		printk("%s: device not ready.\n", wdt->name);
		return;
	}

	struct wdt_timeout_cfg wdt_config = {
		/* Reset SoC when watchdog timer expires. */
		.flags = WDT_FLAG_RESET_SOC,

		/* Expire watchdog after max window */
		.window.min = WDT_MIN_WINDOW,
		.window.max = WDT_MAX_WINDOW,
	};

	wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
	if (wdt_channel_id == -ENOTSUP) {
		/* IWDG driver for STM32 doesn't support callback */
		printk("Callback support rejected, continuing anyway\n");
		wdt_config.callback = NULL;
		wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
	}
	if (wdt_channel_id < 0) {
		printk("Watchdog install error\n");
		return;
	}

	err = wdt_setup(wdt, WDT_OPT);
	if (err < 0) {
		printk("Watchdog setup error\n");
		return;
	}

	while (1) {
		wdt_feed(wdt, wdt_channel_id);
		k_sleep(K_MSEC(WDG_FEED_INTERVAL));
	}
}

#define STACKSIZE 1024
#define PRIORITY  14
K_THREAD_DEFINE(watchdog_thread_id, STACKSIZE, watchdog_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
