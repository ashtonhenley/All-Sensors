/*
 * state_machine.c
 *
 *  Created on: Nov 12, 2025
 *      Author: ashtonhenley
 */

#include "state_machine.h"
#include "main.h"
#include "DS18B20.h"
#include "DS3231(CLK).h"
#include "turbidity.h"
#include "waterlevel.h"
#include "ph.h"
#include <math.h>

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c3;

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

extern uint16_t adc_buffer [2];
extern SensorValues sensorvalues;
extern DateTimeStruct curr_date_time;
extern ScheduledWaterChange sched_date_time;
extern bool water_change_flag;
extern CooldownStruct cooldown;
uint8_t flow_rate = 21; // Time it takes to pump 1 gallon in seconds

typedef enum{
	IDLE_STATE,
	WATER_RES_STATE,
	HEATING_STATE,
	AQUA_DRAIN_STATE,
	AQUA_FILL_STATE
}HeatingState;

// Start in idle state
HeatingState current_state = IDLE_STATE;
// Defines going into the beginning of the next state
static uint8_t state_entry = 1;
// Create an instance of ConditionFlags and initialize to 0
ConditionFlags tankFlags = {0};
// Create an instance of OutOfRangeValues and initialize to preset values
OutOfRangeValues rangevalues = {
		.turbidity = 15,
		.low_ph = 6.5,
		.high_ph = 8.5
};
void sched_curr_time(){
	// If any of these fail, we immediately leave
	if(cooldown.cooldown_flag == 0){
		if (curr_date_time.year    == sched_date_time.year  &&
			curr_date_time.month   == sched_date_time.month &&
			curr_date_time.day     == sched_date_time.day   &&
			curr_date_time.hours   == sched_date_time.hours &&
			curr_date_time.minutes == sched_date_time.minutes)
		{
			current_state = WATER_RES_STATE;
			state_entry = 1;
			water_change_flag = 1;
		}
	}
}
void check_envir_flags(uint16_t turbidity_value, float ph_value){
	tankFlags.low_ph = (sensorvalues.ph_value < rangevalues.low_ph);
	tankFlags.high_ph = (sensorvalues.ph_value > rangevalues.high_ph);
	tankFlags.turbidity = (sensorvalues.turbidity_value > rangevalues.turbidity);
}

void is_flag_high(){
	if(cooldown.cooldown_flag == 0){
		if(tankFlags.turbidity || tankFlags.low_ph || tankFlags.high_ph){
			// If ANY flag is high, start water change, also reset all flags.
			current_state = WATER_RES_STATE;
			state_entry = 1;
			water_change_flag = 1;
			// Reset all flags
			tankFlags.low_ph = 0;
			tankFlags.high_ph = 0;
			tankFlags.turbidity = 0;
		}
	}
}
void handle_water_state(){

	switch(current_state){

	case(IDLE_STATE):
	{
		static uint32_t idle_sod = 0;

	    // Initialize timer once when entering IDLE
	    if (state_entry)
	    {
	        idle_sod =
	            (uint32_t)curr_date_time.hours   * 3600u +
	            (uint32_t)curr_date_time.minutes * 60u +
	            (uint32_t)curr_date_time.seconds;

	        state_entry = 0;
	    }

	    // Check if 60 seconds have elapsed
	    bool time_reached = timer_expired(
	        idle_sod,
	        60u,   // every minute
	        curr_date_time.hours,
	        curr_date_time.minutes,
	        curr_date_time.seconds
	    );

	    if (time_reached)
	    {
	        // Run once-per-minute tasks
	        DS18B20_SampleTemp(&huart3);
	        sensorvalues.temperature_tank = DS18B20_ReadTemp(&huart3);

	        check_turbidity(&sensorvalues.turbidity_value);
	        read_ph(&sensorvalues.ph_value);

	        check_envir_flags(sensorvalues.turbidity_value, sensorvalues.ph_value);
	        is_flag_high();
	        sched_curr_time();

	        // Reset the timer to now
	        idle_sod =
	            (uint32_t)curr_date_time.hours   * 3600u +
	            (uint32_t)curr_date_time.minutes * 60u +
	            (uint32_t)curr_date_time.seconds;

	    }
	}
	break;

		case(WATER_RES_STATE):
		{
	    check_water_level(&sensorvalues.waterlevel_res, &hi2c2);

	    if (sensorvalues.waterlevel_res > 30)
	    {
	        current_state = AQUA_DRAIN_STATE;
	        state_entry = 1;
	    	}
		}
		break;

		case(AQUA_DRAIN_STATE):
						{
			// Need to drain aquarium to certain level
			// Set outbound pump high
				static uint32_t drain_sod = 0;
				if (state_entry)
				{
					// Start outbound pump
					HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
					// How many seconds?
					drain_sod = (uint32_t)curr_date_time.hours   * 3600u +
					        (uint32_t)curr_date_time.minutes * 60u +
					        (uint32_t)curr_date_time.seconds;
					state_entry = 0;
				}

			if(state_entry == 0)
				{
					// Need to ensure that tank doesn't get drained past the water level sensor.
					// The pump should only take 21 seconds to drain 1 gallon from the tank.
					check_water_level(&sensorvalues.waterlevel_tank, &hi2c1);
					// Flag for if it is time to begin

					bool time_reached = timer_expired(drain_sod, flow_rate, curr_date_time.hours, curr_date_time.minutes, curr_date_time.seconds);
					if(time_reached || (sensorvalues.waterlevel_tank <= 20)){
						HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
						// Proceed to next state (HEATING_STATE)
						current_state = HEATING_STATE;
						state_entry = 1;
						break;
						}

					}
			}
			break;
		case(HEATING_STATE):
							{
			// Need to heat water sufficiently
			// Set heater high for certain amount of time, or until we reach a certain temp.
					// If the water in the reservoir is cooler than the water in the tank, turn on heater
				DS18B20_SampleTemp(&huart1);
			    sensorvalues.temperature_res = DS18B20_ReadTemp(&huart1);

			    DS18B20_SampleTemp(&huart3);
			    sensorvalues.temperature_tank = DS18B20_ReadTemp(&huart3);

			    float diff = sensorvalues.temperature_res -
			                 sensorvalues.temperature_tank;
			    if (fabsf(diff) <= 1.0f)
			       {
			           // Within ±1 degree F, stop heating
			           HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET);

			           current_state = AQUA_FILL_STATE;
			           state_entry   = 1;
			       }
			       else
			       {
			           // Not within range, keep heating
			           HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_SET);
			       }
			   }
			   break;
		case(AQUA_FILL_STATE):
							{
				static uint32_t fill_sod = 0;
			// Need to fill aquarium to certain level
				if (state_entry)
		            {
		                // Start inbound pump
		                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET);
		                fill_sod = (uint32_t)curr_date_time.hours   * 3600u +
		                	       (uint32_t)curr_date_time.minutes * 60u +
		                	       (uint32_t)curr_date_time.seconds;

		                state_entry = 0;
		            }
					DS18B20_SampleTemp(&huart3);
						sensorvalues.temperature_tank = DS18B20_ReadTemp(&huart3);
					DS18B20_SampleTemp(&huart1);
			    		sensorvalues.temperature_res = DS18B20_ReadTemp(&huart1);
				// If we fall out of range, go back to heating
					if(sensorvalues.temperature_res < sensorvalues.temperature_tank){
						// Turn off inbound pump
						 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
						 // Go back to heating stage
				         current_state = HEATING_STATE;
				         state_entry = 1;
				         break;
				       }

					if(state_entry == 0)
					{
						// Need to ensure that tank doesn't get drained past the water level sensor.
						// The pump should only take 21 seconds to drain 1 gallon from the tank.
						check_water_level(&sensorvalues.waterlevel_tank, &hi2c1);
						// Flag for if it is time to begin
						bool time_reached = timer_expired(fill_sod, flow_rate, curr_date_time.hours, curr_date_time.minutes, curr_date_time.seconds);

						if(time_reached || (sensorvalues.waterlevel_tank >= 80)){
							HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
							// Proceed to next state (HEATING_STATE)
							current_state = IDLE_STATE;
							state_entry = 1;
							water_change_flag = 0;

							// To set timeout, we need to get current time
							cooldown.cooldown_flag = 1;
							cooldown.cooldown_sod =
							    (uint32_t)curr_date_time.hours   * 3600u +
							    (uint32_t)curr_date_time.minutes * 60u +
							    (uint32_t)curr_date_time.seconds;
						break;
						}
					}
		}
			break;
	}
}
