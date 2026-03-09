/*
 * state_machine.c
 *
 *  Created on: Nov 12, 2025
 *      Author: ashtonhenley
 */

#include "state_machine.h"
#include "main.h"
#include "DS3231(CLK).h"
#include "state_logic.h"
#include "physical_controls.h"

extern ScheduledWaterChange sched_date_time;
extern CooldownStruct cooldown;
extern DateTimeStruct curr_date_time;
extern bool water_change_flag;
extern SensorValues sensorvalues;

// Start in idle state
HeatingState current_state = IDLE_STATE;
// Create an instance of ConditionFlags and initialize to 0
ConditionFlags tankFlags = {0};
// Create an instance of OutOfRangeValues and initialize to preset values
OutOfRangeValues rangevalues =
{
	.max_temp = 80,
    .turbidity = 15,
    .low_ph    = 6.5,
    .high_ph   = 8.5
};

void sched_curr_time()
{
    if (cooldown.cooldown_flag == 0)
    {
        if (curr_date_time.year    == sched_date_time.year  &&
            curr_date_time.month   == sched_date_time.month &&
            curr_date_time.day     == sched_date_time.day   &&
            curr_date_time.hours   == sched_date_time.hours &&
            curr_date_time.minutes == sched_date_time.minutes)
        {
        	// Reset fan
        	fan_low();
            current_state = WATER_RES_STATE;
            state_enter();
            water_change_flag = 1;
        }
    }
}

void check_envir_flags()
{
    tankFlags.low_ph    = (sensorvalues.ph_value < rangevalues.low_ph);
    tankFlags.high_ph   = (sensorvalues.ph_value > rangevalues.high_ph);
    tankFlags.turbidity = (sensorvalues.turbidity_value > rangevalues.turbidity);
}

void is_flag_high()
{
    if (cooldown.cooldown_flag == 0)
    {
        if (tankFlags.turbidity || tankFlags.low_ph || tankFlags.high_ph)
        {
        	// Reset fan
        	fan_low();
            current_state     = WATER_RES_STATE;
            state_enter();
            water_change_flag = 1;

            tankFlags.low_ph    = 0;
            tankFlags.high_ph   = 0;
            tankFlags.turbidity = 0;
        }
    }
}

void handle_water_state()
{
    switch (current_state)
    {
        case IDLE_STATE:
        {
            idle_state();
        }
        break;

        case WATER_RES_STATE:
        {
        	water_res_state();
        }
        break;

        case AQUA_DRAIN_STATE:
        {
            water_drain_state();
        }
        break;

        case HEATING_STATE:
        {
        	heating_state();
        }
        break;

        case AQUA_FILL_STATE:
        {
        	water_fill_state();
        break;
    }
    }
}
