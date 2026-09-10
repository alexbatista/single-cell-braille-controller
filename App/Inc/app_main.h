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
#include "stm32f1xx_hal_spi.h"
#include "stm32f1xx_hal_tim.h"

/**
 * @defgroup app_main Application
 * @brief    Top-level init and run hooks for the main loop.
 * @{
 */

/**
 * @brief One-time application setup.
 *
 * Brings up both motors and homes the discs, then -- in the MODBUS variant --
 * the buzzer, buttons, reader and PLC link. The last two parameters are
 * unused in the USB_CDC variant.
 *
 * @param huart_m1     UART bound to motor 1's TMC2209.
 * @param huart_m2     UART bound to motor 2's TMC2209.
 * @param htim_m1      STEP timer for motor 1.
 * @param htim_m2      STEP timer for motor 2.
 * @param htim_buzzer  PWM timer driving the buzzer.
 * @param hspi_eth     SPI peripheral wired to the W5500.
 */
void App_init(UART_HandleTypeDef *huart_m1, UART_HandleTypeDef *huart_m2,
              TIM_HandleTypeDef *htim_m1, TIM_HandleTypeDef *htim_m2,
              TIM_HandleTypeDef *htim_buzzer, SPI_HandleTypeDef *hspi_eth);

/**
 * @brief One pass of the cooperative main loop; called forever.
 *
 * In the MODBUS variant this services buttons, the PLC link and the
 * presentation sequencer, in that order, and returns. It blocks only while
 * the discs are turning or a buzzer pattern is playing.
 */
void App_run(void);

/** @} */ // end of app_main

#endif // __APP_MAIN__
