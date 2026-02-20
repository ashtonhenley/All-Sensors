/*
 * state_machine.h
 *
 *  Created on: Nov 12, 2025
 *      Author: ashtonhenley
 */

#ifndef INC_STATE_MACHINE_H_
#define INC_STATE_MACHINE_H_
#include <stdbool.h>
#include <stdint.h>
typedef struct{
	bool turbidity;
	bool low_ph;
	bool high_ph;
}ConditionFlags;

typedef struct{
	uint8_t turbidity;
	uint8_t low_ph;
	uint8_t high_ph;
}OutOfRangeValues;
void sched_curr_time();
void is_flag_high();
void handle_water_state();
void check_envir_flags(uint16_t turbidity_value, float ph_value);
#endif /* INC_STATE_MACHINE_H_ */
