#ifndef MOTION_PLANNER_H
#define MOTION_PLANNER_H

#include <stdint.h>

#include "stm32f1xx_hal.h"

typedef struct {
  uint8_t single_row_disc;
  uint8_t double_row_disc;
} disk_angles_t;

void initialize_motors(UART_HandleTypeDef *huart_01,
                       UART_HandleTypeDef *huart_02);

void Calibrate_Zero_Position(void);

void move_to_angle(disk_angles_t disk_angle);

void test_rotate_motor(void);

#endif
