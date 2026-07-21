#ifndef MOTION_PLANNER_H
#define MOTION_PLANNER_H

#include <stdint.h>

void Calibrate_Zero_Position(void);

void Move_To_Position(uint16_t steps, uint8_t direction);

#endif
