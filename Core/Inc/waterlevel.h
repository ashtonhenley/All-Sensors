/*
 * waterlevel.h
 *
 *  Created on: Jan 13, 2026
 *      Author: ashtonhenley
 */
#include "main.h"
#include "stdint.h"

#ifndef SRC_WATERLEVEL_H_
#define SRC_WATERLEVEL_H_

#define WATER_LOW_ADDR 0xEE
#define WATER_HIGH_ADDR 0xF0
#define THRESHOLD 100 // changed from 5
#define NO_TOUCH 0xFE


void waterlevel_low_read(uint8_t *data, I2C_HandleTypeDef *hi2c);
void waterlevel_high_read(uint8_t *data, I2C_HandleTypeDef *hi2c);

void check_water_level(uint8_t *water_level, I2C_HandleTypeDef *hi2c);


#endif /* SRC_WATERLEVEL_H_ */
