#ifndef MOTION_PLANNER_H
#define MOTION_PLANNER_H

#include <stdint.h>

#include "stm32f1xx_hal.h"

// Absolute disc angles in tenths of a degree (0..3599), matching the
// DISC_ANGLE_TO_POS_ANG_x scale in braille_disc.h. A full turn is 3600, so
// these do not fit in a uint8_t.
typedef struct {
  uint16_t single_row_disc;
  uint16_t double_row_disc;
} disk_angles_t;

void initialize_motors(UART_HandleTypeDef *huart_01,
                       UART_HandleTypeDef *huart_02, TIM_HandleTypeDef *htim_01,
                       TIM_HandleTypeDef *htim_02);

void Calibrate_Zero_Position(void);

void move_to_angle(disk_angles_t disk_angle);

void test_rotate_motor(void);

#endif
