#ifndef __APP_HHS_MATH_H__
#define __APP_HHS_MATH_H__

#include <zephyr/kernel.h>

typedef struct moving_average {
	int32_t sum;
	int pos;
	int *buffer;
	int length;
	bool is_filled;
} moving_average_t;

int movingAvg(moving_average_t *av_obj, int new_element);
moving_average_t *allocate_moving_average(const int len);
void free_moving_average(moving_average_t *av_obj);

/** A point in a battery discharge curve sequence.
 *
 * A discharge curve is defined as a sequence of these points, where
 * the first point has #lvl_pptt set to 10000 and the last point has
 * #lvl_pptt set to zero.  Both #lvl_pptt and #lvl_mV should be
 * monotonic decreasing within the sequence.
 */
struct level_point {
	/** Remaining life at #lvl_mV. */
	int16_t lvl_pptt;

	/** Battery voltage at #lvl_pptt remaining life. */
	int16_t lvl_mV;
};

unsigned int level_pptt(unsigned int batt_mV, const struct level_point *curve);

#endif
