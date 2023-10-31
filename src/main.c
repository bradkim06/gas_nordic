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

#include "bluetooth.h"
#include "version.h"

LOG_MODULE_REGISTER(MAIN, CONFIG_APP_LOG_LEVEL);

const unsigned char fw_info[] = {
	VERSION_MAJOR_INIT,
	'.',
	VERSION_MINOR_INIT,
	'.',
	VERSION_PATCHLEVEL_INIT,
	'v',
	' ',
	BUILD_YEAR_CH0,
	BUILD_YEAR_CH1,
	BUILD_YEAR_CH2,
	BUILD_YEAR_CH3,
	'-',
	BUILD_MONTH_CH0,
	BUILD_MONTH_CH1,
	'-',
	BUILD_DAY_CH0,
	BUILD_DAY_CH1,
	'T',
	BUILD_HOUR_CH0,
	BUILD_HOUR_CH1,
	':',
	BUILD_MIN_CH0,
	BUILD_MIN_CH1,
	':',
	BUILD_SEC_CH0,
	BUILD_SEC_CH1,
	'\0',
};

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
	LOG_INF("Firmware Info : %s", fw_info);
	LOG_INF("Board:%s SoC:%s Rom:%dkb Ram:%dkb", CONFIG_BOARD, CONFIG_SOC, CONFIG_FLASH_SIZE,
		CONFIG_SRAM_SIZE);

	return 0;
}
