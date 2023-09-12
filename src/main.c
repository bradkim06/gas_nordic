/**
 * @file main.c
 * @brief
 * @author bradkim06
 * @version 0.01
 * @date 2023-09-12
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

int main(void)
{
	LOG_INF("Hello World! %s\n", CONFIG_BOARD);
	return 0;
}
