#ifndef __APP_HHS_MATH_H__
#define __APP_HHS_MATH_H__

#include <zephyr/kernel.h>

typedef struct moving_average {
	/* sum of all buffer data */
	int32_t sum;
	/* current buffer position */
	int current_position;
	/* Data storage array */
	int *buffer;
	/* Data Buffer Size */
	int buffer_length;
	/* Becomes true if the buffer is filled at least once */
	bool is_filled;
} moving_average_t;

/**
 * @brief Moving Average Filter Function
 *
 * @param av_obj: The moving average filter structure with the addition of new data.
 * @param new_element: new data
 *
 * @return average data value
 */
int calculate_moving_average(moving_average_t *av_obj, int new_element);

/**
 * @brief Dynamic allocation of moving average filter structures
 *
 * @param len: number of moving average filters
 *
 * @return The allocated moving average filter structure.
 */
moving_average_t *allocate_moving_average(const int len);

/**
 * @brief Release the dynamically allocated moving average filter structure
 *
 * @param av_obj: The moving average filter structure to be released
 */
void free_moving_average(moving_average_t *av_obj);

/** A dataset for converting ADC mV data to pptt
 *
 * Defined as a sequence of multiple points where each point should be transitioned
 * from larger values to smaller ones (Both #lvl_pptt and #lvl_mV should be
 * monotonic decreasing within the sequence.)
 */
struct level_point {
	/** Remaining life at #lvl_mV. */
	int16_t lvl_pptt;

	/** Battery voltage at #lvl_pptt remaining life. */
	int16_t lvl_mV;
};

/** Calculate the estimated pptt level based on a measured voltage.
 *
 * @param mV: a measured adc voltage level.
 *
 * @param curve: the curve for the type of level_point
 *
 * @return the estimated curved pptt
 */
unsigned int level_pptt(unsigned int mV, const struct level_point *curve);

#endif
