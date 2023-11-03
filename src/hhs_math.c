#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hhs_math.h"

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

unsigned int calculate_level_pptt(unsigned int voltage_mV, const struct level_point *curvePoints)
{
	const struct level_point *currentPoint = curvePoints;

	/* If the measured voltage is above the highest point, cap at maximum. */
	if (voltage_mV >= currentPoint->lvl_mV) {
		return currentPoint->lvl_pptt;
	}

	/* Iterate to the last point at or below the measured voltage. */
	while ((currentPoint->lvl_pptt > 0) && (voltage_mV < currentPoint->lvl_mV)) {
		++currentPoint;
	}

	/* If the measured voltage is below the lowest point, cap at minimum. */
	if (voltage_mV < currentPoint->lvl_mV) {
		return currentPoint->lvl_pptt;
	}

	/* Perform linear interpolation between the points below and above the measured voltage. */
	const struct level_point *previousPoint = currentPoint - 1;

	return currentPoint->lvl_pptt + ((previousPoint->lvl_pptt - currentPoint->lvl_pptt) *
					 (voltage_mV - currentPoint->lvl_mV) /
					 (previousPoint->lvl_mV - currentPoint->lvl_mV));
}
