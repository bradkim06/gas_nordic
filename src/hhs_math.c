#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hhs_math.h"

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
int calculate_moving_average(moving_average_t *av_obj, int new_element)
{
	/* Subtract the oldest number from the previous sum, add the new number */
	av_obj->sum = av_obj->sum - av_obj->buffer[av_obj->current_position] + new_element;
	/* Assign the new_element to the current_position in the buffer array */
	av_obj->buffer[av_obj->current_position] = new_element;
	/* Increment the current_position */
	av_obj->current_position++;
	if (av_obj->current_position >= av_obj->buffer_length) {
		/* Reset the current_position and set is_filled to true when the buffer is filled */
		av_obj->current_position = 0;
		av_obj->is_filled = true;
	}

	/* Calculate and return the average */
	return (int)round(
		((double)av_obj->sum /
		 (double)(av_obj->is_filled ? av_obj->buffer_length : av_obj->current_position)));
}

/**
 * @brief Allocates and initializes a moving_average_t object.
 *
 * This function allocates memory for a moving_average_t object and its buffer.
 * It also initializes the members of the object.
 *
 * @param len The length of the buffer to be allocated.
 * @return A pointer to the allocated moving_average_t object.
 */
moving_average_t *allocate_moving_average(const int buffer_length)
{
	/* Allocate memory for the moving_average_t object */
	moving_average_t *moving_average_obj = malloc(sizeof(moving_average_t));

	/* Check if memory allocation was successful */
	if (moving_average_obj == NULL) {
		return NULL;
	}

	/* Initialize the members of the moving_average_t object */
	moving_average_obj->sum = 0;
	moving_average_obj->current_position = 0;
	moving_average_obj->buffer_length = buffer_length;
	moving_average_obj->is_filled = false;

	/* Allocate memory for the buffer and initialize it to 0 */
	moving_average_obj->buffer = calloc(buffer_length, sizeof(int));

	/* Check if memory allocation was successful */
	if (moving_average_obj->buffer == NULL) {
		free(moving_average_obj);
		return NULL;
	}

	/* Return the pointer to the allocated moving_average_t object */
	return moving_average_obj;
}

/**
 * @brief Frees the memory allocated for a moving_average_t object.
 *
 * This function frees the memory allocated for the buffer of the moving_average_t object
 * and the object itself. It also sets the pointer to the object to NULL to prevent
 * dangling pointers.
 *
 * @param avg_obj Pointer to the moving_average_t object to be freed.
 */
void free_moving_average(moving_average_t **avg_obj)
{
	if (avg_obj == NULL || *avg_obj == NULL) {
		return;
	}

	free((*avg_obj)->buffer);
	(*avg_obj)->buffer = NULL;

	free(*avg_obj);
	*avg_obj = NULL;
}

unsigned int level_pptt(unsigned int batt_mV, const struct level_point *curve)
{
	const struct level_point *pb = curve;

	if (batt_mV >= pb->lvl_mV) {
		/* Measured voltage above highest point, cap at maximum. */
		return pb->lvl_pptt;
	}
	/* Go down to the last point at or below the measured voltage. */
	while ((pb->lvl_pptt > 0) && (batt_mV < pb->lvl_mV)) {
		++pb;
	}
	if (batt_mV < pb->lvl_mV) {
		/* Below lowest point, cap at minimum */
		return pb->lvl_pptt;
	}

	/* Linear interpolation between below and above points. */
	const struct level_point *pa = pb - 1;

	return pb->lvl_pptt +
	       ((pa->lvl_pptt - pb->lvl_pptt) * (batt_mV - pb->lvl_mV) / (pa->lvl_mV - pb->lvl_mV));
}
