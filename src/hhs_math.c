#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zephyr/logging/log.h>

#include "hhs_math.h"

/* Module registration for Gas Monitor with the specified log level. */
LOG_MODULE_REGISTER(HHS_MATH, CONFIG_APP_LOG_LEVEL);

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
#define MAX_LEVEL_POINTS 100 // curvePoints 배열에서 최대 점 개수 (안전 검사용)
	/* NULL 포인터 검사 */
	if (curvePoints == NULL) {
		LOG_ERR("calculate_level_pptt: curvePoints pointer is NULL");
		return 0;
	}

	const struct level_point *currentPoint = curvePoints;
	int iterations = 0;

	/* 측정 전압이 최고점 이상의 경우, 최대 레벨로 제한 */
	if (voltage_mV >= currentPoint->lvl_mV) {
		return currentPoint->lvl_pptt;
	}

	/* 무한 루프 방지를 위해 최대 반복 횟수를 제한하면서, 측정 전압보다 큰 구간을 탐색 */
	while ((currentPoint->lvl_pptt > 0) && (voltage_mV < currentPoint->lvl_mV)) {
		++currentPoint;
		iterations++;
		if (iterations > MAX_LEVEL_POINTS) {
			LOG_ERR("calculate_level_pptt: Exceeded maximum iterations. Check "
				"curvePoints array.");
			break;
		}
	}

	/* 측정 전압이 최저점 이하인 경우, 최소 레벨로 제한 */
	if (voltage_mV < currentPoint->lvl_mV) {
		return currentPoint->lvl_pptt;
	}

	/* 선형 보간을 위해 이전 점이 존재하는지 확인 */
	if (currentPoint == curvePoints) {
		LOG_ERR("calculate_level_pptt: Not enough curve points for interpolation.");
		return currentPoint->lvl_pptt;
	}

	const struct level_point *previousPoint = currentPoint - 1;

	/* 분모가 0인 경우(두 점의 전압값이 같은 경우) 방지 */
	if (previousPoint->lvl_mV == currentPoint->lvl_mV) {
		LOG_ERR("calculate_level_pptt: Division by zero in interpolation. Identical lvl_mV "
			"values.");
		return currentPoint->lvl_pptt;
	}

	/* 선형 보간 계산 */
	return currentPoint->lvl_pptt + ((previousPoint->lvl_pptt - currentPoint->lvl_pptt) *
					 (voltage_mV - currentPoint->lvl_mV) /
					 (previousPoint->lvl_mV - currentPoint->lvl_mV));
}
