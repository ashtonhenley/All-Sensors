/*
 * state_logic.c
 *
 *  Created on: Mar 5, 2026
 *      Author: ashtonhenley
 */


#include "state_logic.h"
#include "main.h"
#include "state_machine.h"
#include "DS3231(CLK).h"
#include "mcp23017.h"
#include "turbidity.h"
#include "DS18B20.h"
#include "ph.h"
#include "waterlevel.h"
#include <math.h>
#include "physical_controls.h"


// Defines going into the beginning of the next state
static uint8_t state_entry = 1;

// I2C Handles
extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c3;

// UART Handles
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;

// External Structs
extern CooldownStruct cooldown;
extern HeatingState current_state;
extern SensorValues sensorvalues;
extern DateTimeStruct curr_date_time;
extern ScheduledWaterChange sched_date_time;
extern OutOfRangeValues rangevalues;
// All external variables
extern bool water_change_flag;

static uint32_t fill_elapsed = 0;   // total pump runtime accumulated
static uint32_t fill_start_sod = 0; // start time of the current run

uint8_t flow_rate = 21;   // Time it takes to pump 1 gallon in seconds

float FAN_ON_TEMP;
float FAN_OFF_TEMP;
static bool fan_on = 0;

void state_enter(){
	state_entry = 1;
}
void state_leave(){
	state_entry = 0;
}

uint32_t get_seconds_of_day(void)
{
    return (uint32_t)curr_date_time.hours * 3600u +
           (uint32_t)curr_date_time.minutes * 60u +
           (uint32_t)curr_date_time.seconds;
}

void idle_state(){

	static uint32_t idle_sod = 0;
	// Initialize range values for fan

	if(state_entry){
		// Get the current time
		idle_sod = get_seconds_of_day();
		FAN_ON_TEMP = rangevalues.max_temp;
		FAN_OFF_TEMP = rangevalues.max_temp - 2;
		state_leave();
	}
	// Check... have we reached our time?
	 bool time_reached = timer_expired(
	                idle_sod,
	                1u,     // Time in seconds that determines the interval of time_reached, also controls how often we sample sensors
	                curr_date_time.hours,
	                curr_date_time.minutes,
	                curr_date_time.seconds
	 );
     if (time_reached)
     {
     	// If it's time to sample sensors, take samples
    	 sample_temperature_sensors();
    	 check_turbidity(&sensorvalues.turbidity_value);
         read_ph(&sensorvalues.ph_value);
         check_water_level(&sensorvalues.waterlevel_tank, &hi2c1);
         check_water_level(&sensorvalues.waterlevel_res, &hi2c2);
         // Need to check if these sensors values are out of range
        // check_envir_flags();
         is_flag_high();
         // Need to check if we have a water change scheduled for this time
         sched_curr_time();

         // Reset our start time
         idle_sod = get_seconds_of_day();
     }
     /* Are we over max temperature allowed?
      * if so, turn on fan, otherwise, turn off fan
      */

     if(!fan_on && sensorvalues.temperature_tank > FAN_ON_TEMP)
     {
         fan_high();
         fan_on = 1;
     }
     else if(fan_on && sensorvalues.temperature_tank < FAN_OFF_TEMP)
     {
         fan_low();
         fan_on = 0;
     }
}

void water_res_state(){
	// Check water level of reservoir
	check_water_level(&sensorvalues.waterlevel_res, &hi2c2);

	// If water level is sufficient (30%), move to water drain state
	if (sensorvalues.waterlevel_res > 30)
	   {
	     current_state = AQUA_DRAIN_STATE;
	     state_enter();
	   }
}

void water_drain_state(){
	static uint32_t drain_sod = 0;

	 if (state_entry)
	    {
	       // Set outbound pump high
	       outbound_pump_high();

	       // Get start time of outbound pump
	       drain_sod = get_seconds_of_day();
	       state_leave();
	    }
	 if (state_entry == 0)
	             {
	       // Check water level of main tank to ensure we don't over drain
	       check_water_level(&sensorvalues.waterlevel_tank, &hi2c1);

	       // Check to see if flow rate in seconds has been reached
	       bool time_reached = timer_expired(
	            drain_sod,
	            flow_rate,
	            curr_date_time.hours,
	            curr_date_time.minutes,
	            curr_date_time.seconds
	             );

	       /* If we have reached one gallon drained or if the level of the tank is lower than 20%,
	        * turn off outbound pump and move to heating state
	        */
	       if (time_reached || (sensorvalues.waterlevel_tank <= 20))
	          {
	           outbound_pump_low();

	          current_state = HEATING_STATE;
	          state_enter();
	          }
	     }
}

void heating_state(){
		// Sample both temperature sensors
		sample_temperature_sensors();

	    // Calculate the difference between the two temperatures
	    float diff = sensorvalues.temperature_res -
	                 sensorvalues.temperature_tank;
	           /* If the difference between the two is greater than or equal to +1 Deg. F we will turn off
	             * heater & circulating pump, otherwise, we will turn them on / keep them on
	             * Also, we can change the diff value in the if statement to make up for
	             * the heating element still being hot after being turned off
	             */

	     if (diff >= 0.0f && fabsf(diff) <= 1.0f)
	     	 {
	            heater_low();
	            circulating_pump_low();

	            current_state = AQUA_FILL_STATE;
	            state_enter();
	         }
	            else
	         {
	            /* Turn on heater & circulating pump
	            * MCP23017 is setting PA0 High
	            */
	            heater_high();
	            circulating_pump_high();
	         }
	}

void water_fill_state(){
   	// Only perform these commands once
    if (state_entry)
       {
    		inbound_pump_high();

            fill_start_sod = get_seconds_of_day();
            state_leave();
            }

            // Compute time once per loop
            uint32_t curr_sod = get_seconds_of_day();

            uint32_t elapsed = (curr_sod >= fill_start_sod)
                ? (curr_sod - fill_start_sod)
                : (86400u - fill_start_sod + curr_sod);   // midnight rollover safe

            // Monitor temperatures while filling
            sample_temperature_sensors();
            float diff = sensorvalues.temperature_res - sensorvalues.temperature_tank;

            // If temp mismatch gets too large, pause filling and go back to heating
            if (diff < -1.0f)
            {
            	inbound_pump_low();

                fill_elapsed += elapsed;   // accumulate pump runtime so far

                current_state = HEATING_STATE;
                state_enter();
            }

            // Continue fill checks
            check_water_level(&sensorvalues.waterlevel_tank, &hi2c1);

            // Does the time elapsed now plus any previous pump time add to our flow rate?
            bool time_reached = ((fill_elapsed + elapsed) >= flow_rate);

            if (time_reached || (sensorvalues.waterlevel_tank >= 80))
            {
            	inbound_pump_low();

                fill_elapsed = 0; // reset for next water change

                current_state     = IDLE_STATE;
                state_enter();
                water_change_flag = 0;

                cooldown.cooldown_flag = 1;
                cooldown.cooldown_sod = get_seconds_of_day();
            }
    }

