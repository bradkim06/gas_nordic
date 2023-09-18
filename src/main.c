/**
 * @file main.c
 * @brief
 * @author bradkim06
 * @version 0.01
 * @date 2023-09-12
 */

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "led.h"

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_DBG);

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
	led_ctrl(state_g, true);
	LOG_INF("Hello World! %s\n", CONFIG_BOARD);

	while (true) {
		LOG_INF("main thread");
		k_msleep(60 * MSEC_PER_SEC);
	}

	return 0;
}
