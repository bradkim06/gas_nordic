#ifndef __APP_BATTERY_H__
#define __APP_BATTERY_H__

#define LOW_BATT_THRESHOLD 2000

struct batt_value {
	/** Integer part of the value. Range 0~100*/
	unsigned int val1;
	/** Fractional part of the value. Range 0~99 */
	unsigned int val2;
};

/**
 * @brief Get the battery percentage value.
 *
 * This function retrieves the battery percentage value after acquiring the necessary semaphore to
 * ensure exclusive access to the battery data. It returns a copy of the battery percentage value.
 *
 * @return A copy of the battery percentage value.
 */
struct batt_value get_batt_percent(void);

#endif
