/*
 * waterlevel.c
 *
 *  Created on: Jan 13, 2026
 *      Author: ashtonhenley
 */

#include "waterlevel.h"
#include "stdint.h"
#include "main.h"

//extern I2C_HandleTypeDef hi2c1;

void waterlevel_low_read(uint8_t *data_low, I2C_HandleTypeDef *hi2c){
	HAL_I2C_Master_Receive(hi2c, WATER_LOW_ADDR, data_low, 8, HAL_MAX_DELAY);
}

void waterlevel_high_read(uint8_t *data_high, I2C_HandleTypeDef *hi2c){
	HAL_I2C_Master_Receive(hi2c, WATER_HIGH_ADDR, data_high, 12, HAL_MAX_DELAY);
}

void check_water_level(uint8_t *water_level, I2C_HandleTypeDef *hi2c){
	// Reset count
	uint32_t touch_val = 0;
	uint8_t trig_section = 0;

	uint8_t data_low[8] = {0};
	uint8_t data_high[12] = {0};

	// Get new values
	waterlevel_low_read(data_low, hi2c);
	HAL_Delay(10);
	waterlevel_high_read(data_high, hi2c);

	// Now compute percentages
	for(int i = 0; i < 8; i++){
		if(data_low[i] > THRESHOLD) touch_val |= 1 << i;
	}
	for (int i = 0 ; i < 12; i++){
	  if (data_high[i] > THRESHOLD) touch_val |= 1 << (8 + i);
	}
	while(touch_val & 0x01){
		trig_section++;
		touch_val >>= 1;
	}
	*water_level = trig_section * 5;
}
