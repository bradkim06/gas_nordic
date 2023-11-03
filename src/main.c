/**
 * @file src/main.c - The starting point of the user application
 *
 * @brief The operation of the main() function, which is the first function called after kernel
 * initialization
 *
 * @author bradkim06@gmail.com
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bluetooth.h"
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
