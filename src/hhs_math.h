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
 * @brief Calculate and return the moving average.
 *
 * This function calculates the moving average of a series of numbers.
 * It subtracts the oldest number from the previous sum, adds the new number,
 * and updates the position in the buffer array. If the position exceeds the length of the array,
 * it resets the position and sets the is_filled flag to true.
 *
 * @param av_obj Pointer to the moving_average_t object.
 * @param new_element The new number to be added to the moving average calculation.
 * @return The calculated moving average as an integer.
 */
int calculate_moving_average(moving_average_t *av_obj, int new_element);

/**
 * @brief Allocates and initializes a moving_average_t object.
 *
 * This function allocates memory for a moving_average_t object and its buffer.
 * It also initializes the members of the object.
 *
 * @param len The length of the buffer to be allocated.
 * @return A pointer to the allocated moving_average_t object.
 */
moving_average_t *allocate_moving_average(const int buffer_length);

/**
 * @brief Frees the memory allocated for a moving_average_t object.
 *
 * This function frees the memory allocated for the buffer of the moving_average_t object
 * and the object itself. It also sets the pointer to the object to NULL to prevent
 * dangling pointers.
 *
 * @param avg_obj Pointer to the moving_average_t object to be freed.
 */
void free_moving_average(moving_average_t **av_obj);

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

/**
 * @brief Calculate the estimated pptt level based on a measured voltage.
 *
 * This function takes a measured ADC voltage level and a curve for the type of level_point as
 * input. It then calculates and returns the estimated curved pptt.
 *
 * This function, `calculate_level_pptt`, is used to calculate a level (in pptt) based on a given
 * voltage (in mV) and a curve of points. The curve of points is represented as an array of
 * `level_point` structures, where each point has a voltage level (`lvl_mV`) and a corresponding
 * pptt level (`lvl_pptt`).
 *
 * Here's a breakdown of the function:
 *
 * 1. The function starts by setting `currentPoint` to the start of the `curvePoints` array.
 *
 * 2. It then checks if the given `voltage_mV` is greater than or equal to the voltage level of the
 * `currentPoint`. If it is, it returns the pptt level of the `currentPoint`. This is because
 * thegiven voltage is above the highest point in the curve, so the function caps the result at the
 * maximum pptt level.
 *
 * 3. If the given voltage is not above the highest point, the function enters a while loop. This
 * loop iterates over the `curvePoints` array until it finds the last point that is at or below the
 * given voltage. It does this by incrementing `currentPoint` as long as the pptt level of the
 * `currentPoint` is greater than 0 and the given voltage is less than the voltage level of the
 * `currentPoint`.
 *
 * 4. After the loop, the function checks if the given voltage is less than the voltage level of the
 * `currentPoint`. If it is, it returns the pptt level of the `currentPoint`. This is because the
 * given voltage is below the lowest point in the curve, so the function caps the result at the
 * minimum pptt level.
 *
 * 5. If the given voltage is not below the lowest point, the function performs linear interpolation
 * to calculate the pptt level corresponding to the given voltage. It does this by finding the
 * `previousPoint` (the point before the `currentPoint`), and then using the formula for linear
 * interpolation to calculate the pptt level.
 * The function then returns this calculated pptt level.
 *
 * In summary, this function is used to find the pptt level corresponding to a given voltage, based
 * on a curve of points. It does this by either capping at the maximum or minimum pptt level (if the
 * voltage is above the highest point or below the lowest point), or by performing linear
 * interpolation between the points below and above the given voltage.
 *
 * @param voltage_mV The measured ADC voltage level in millivolts.
 * @param curvePoints Pointer to the curve for the type of level_point.
 *
 * @return The estimated curved pptt as an unsigned integer.
 */
unsigned int calculate_level_pptt(unsigned int voltage_mV, const struct level_point *curvePoints);

#endif // __APP_HHS_MATH_H__
