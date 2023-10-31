#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hhs_math.h"

int movingAvg(moving_average_t *av_obj, int new_element)
{
	/* Subtract the oldest number from the prev sum, add the new number */
	av_obj->sum = av_obj->sum - av_obj->buffer[av_obj->pos] + new_element;
	/* Assign the nextNum to the position in the array */
	av_obj->buffer[av_obj->pos] = new_element;
	/* Increment position internaly */
	av_obj->pos++;
	if (av_obj->pos >= av_obj->length) {
		av_obj->pos = 0;
		av_obj->is_filled = true;
	}

	/* return the average */
	return (int)round(
		((double)av_obj->sum / (double)(av_obj->is_filled ? av_obj->length : av_obj->pos)));
}

moving_average_t *allocate_moving_average(const int len)
{
	moving_average_t *av_obj = malloc(sizeof(moving_average_t));
	av_obj->sum = 0;
	av_obj->pos = 0;
	av_obj->length = len;
	av_obj->is_filled = false;
	av_obj->buffer = malloc(len * sizeof(int));
	memset(av_obj->buffer, 0, len * sizeof(int));
	return av_obj;
}

void free_moving_average(moving_average_t *av_obj)
{
	free(av_obj->buffer);
	av_obj->buffer = NULL;
	free(av_obj);
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
