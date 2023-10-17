#ifndef __APP_BATTERY_H__
#define __APP_BATTERY_H__

#define LOW_BATT_THRESHOLD 2000

struct batt_value {
	/** Integer part of the value. */
	unsigned int val1;
	/** Fractional part of the value (in one-millionth parts). */
	unsigned int val2;
};

struct batt_value get_batt_percent(void);

#endif
