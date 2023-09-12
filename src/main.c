/**
 * @file main.c
 * @brief
 * @author bradkim06
 * @version 0.01
 * @date 2023-09-12
 */

#include <zephyr/kernel.h>

int main(void)
{
	printk("Hello World! %s\n", CONFIG_BOARD);
	return 0;
}
