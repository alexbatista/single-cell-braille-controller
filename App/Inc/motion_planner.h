/**
 * @file    motion_planner.h
 * @brief   Two-motor motion planner for the braille disc.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * Each motor is a TMC2209 configured over its own UART and moved through its
 * STEP/DIR pins via the @ref stepper module; both motors step in lockstep
 * from the same paced loop. This layer owns the two stepper instances and
 * translates absolute disc angles into signed microstep moves.
 */

#ifndef MOTION_PLANNER_H
#define MOTION_PLANNER_H

#include <stdint.h>

#include "stm32f1xx_hal.h"

/**
 * @defgroup motion_planner Motion Planner
 * @brief    Coordinated two-motor moves for the braille disc.
 * @{
 */

/**
 * @brief Absolute target angle for each disc, in tenths of a degree.
 *
 * Range 0..3599, matching the `DISC_ANGLE_TO_POS_ANG_x` scale in
 * braille_disc.h. A full turn is 3600.
 */
typedef struct {
  uint16_t
      single_row_disc; /**< Target angle of the single-row disc (0.1 deg). */
  uint16_t
      double_row_disc; /**< Target angle of the double-row disc (0.1 deg). */
} disk_angles_t;

/**
 * @brief Bring up both TMC2209 drivers and bind their stepper instances.
 *
 * Boots both drivers disabled, configures them over UART (microstepping and
 * run/hold currents), binds each stepper to its STEP timer and DIR pin, and
 * puts both drivers on the STEP/DIR interface. A driver that fails to answer
 * over UART is flagged with an LED blink pattern but does not halt startup.
 *
 * @param huart_01 UART bound to motor 01's TMC2209.
 * @param huart_02 UART bound to motor 02's TMC2209.
 * @param htim_01  STEP timer for motor 01 (PWM output on its STEP channel).
 * @param htim_02  STEP timer for motor 02 (PWM output on its STEP channel).
 */
void initialize_motors(UART_HandleTypeDef *huart_01,
                       UART_HandleTypeDef *huart_02, TIM_HandleTypeDef *htim_01,
                       TIM_HandleTypeDef *htim_02);

/**
 * @brief Establish the zero reference for both discs.
 * @todo  Not implemented yet.
 */
void calibrate_zero_position(void);

/**
 * @brief Turn both discs to their absolute angles and block until they arrive.
 *
 * The two discs run concurrently, so the move takes as long as the slower
 * disc. Each disc takes the shortest signed path (a target more than half a
 * turn ahead is reached faster by going backwards).
 *
 * @param disk_angle Absolute target angles for both discs (0.1 deg).
 */
void move_to_angle(disk_angles_t disk_angle);

/**
 * @brief Spin both motors one full revolution (diagnostic).
 *
 * Blocks until the revolution completes.
 */
void test_rotate_motor(void);

/** @} */ // end of motion_planner

#endif // MOTION_PLANNER_H
