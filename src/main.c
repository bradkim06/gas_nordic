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

LOG_MODULE_REGISTER(MAIN, LOG_LEVEL_DBG);

int main(void)
{
	LOG_INF("Hello World! %s\n", CONFIG_BOARD);

	while (true) {
		LOG_INF("main thread");
		k_msleep(60 * MSEC_PER_SEC);
	}

	return 0;
}
