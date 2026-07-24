/**
 * @file    app_main.h
 * @brief   Application entry points called from the CubeMX main loop.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * Thin bridge between the generated Core/Src/main.c and the App layer:
 * App_init() wires up the motors once, App_run() performs one unit of motion
 * per call and is invoked forever from the main loop.
 */

#ifndef __APP_MAIN__
#define __APP_MAIN__

#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_tim.h"

/**
 * @defgroup app_main Application
 * @brief    Top-level init and run hooks for the main loop.
 * @{
 */

/**
 * @brief One-time application setup: bring up both motors.
 *
 * @param huart_m1 UART bound to motor 1's TMC2209.
 * @param huart_m2 UART bound to motor 2's TMC2209.
 * @param htim_m1  STEP timer for motor 1.
 * @param htim_m2  STEP timer for motor 2.
 */
void App_init(UART_HandleTypeDef *huart_m1, UART_HandleTypeDef *huart_m2,
              TIM_HandleTypeDef *htim_m1, TIM_HandleTypeDef *htim_m2);

/**
 * @brief Perform one unit of motion; called repeatedly from the main loop.
 */
void App_run(void);

/** @} */ // end of app_main

#endif // __APP_MAIN__
