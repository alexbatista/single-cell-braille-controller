/**
 * @file    stepper_hal.h
 * @brief   STM32 implementation of ::Stepper_Hal_t.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * STEP pulses come from a timer PWM output; DIR and ENN are plain GPIOs
 * written through BSRR. This is the only stepper file that knows about
 * `GPIO_TypeDef` and `TIM_HandleTypeDef`; stepper.h / stepper.c stay platform
 * independent.
 *
 * A motor whose wiring or gearing turns the disc the opposite way to the
 * position counter is corrected here, by the @p invert_dir flag, rather than in
 * the planner: which way DIR turns the load is a board property, and inverting
 * it at the pin keeps "positive steps" meaning "increasing angle" everywhere
 * above this file.
 *
 * The DIR/ENN pins must already be configured as push-pull outputs and the
 * STEP timer must already be initialized in PWM mode with its channel
 * configured and its global interrupt enabled (all done by the CubeMX
 * generated code in Core/Src/gpio.c and Core/Src/tim.c). Pin arguments are
 * `GPIO_PIN_x` masks, so the defines from Core/Inc/main.h can be passed
 * straight in.
 *
 * The STEP pin itself is not passed here: `MX_TIMx_Init()` already puts it in
 * alternate function mode through `HAL_TIM_MspPostInit()`, and from then on it
 * belongs to the timer, not to this port.
 */

#ifndef STEPPER_HAL_H
#define STEPPER_HAL_H

#include "stm32f1xx_hal.h"

#include "stepper.h"

/**
 * @defgroup stepper_port STM32 Stepper Port
 * @ingroup  stepper
 * @brief    STM32 timer/GPIO backing for ::Stepper_Hal_t.
 * @{
 */

/**
 * @brief STM32 port state backing a ::Stepper_Hal_t.
 *
 * One instance per motor; must outlive the ::Stepper_Hal_t (and any
 * ::Stepper_t) initialized from it.
 */
typedef struct Stepper_Hal_Stm32 {
  TIM_HandleTypeDef *step_tim; /**< Timer whose PWM output drives the STEP pin. */
  uint32_t step_channel;       /**< TIM_CHANNEL_x routed to the STEP pin. */
  GPIO_TypeDef *dir_port;      /**< DIR output GPIO port. */
  uint16_t dir_pin;            /**< DIR pin (GPIO_PIN_x mask). */
  bool invert_dir;             /**< Drive DIR inverted (mirrored motor). */
  GPIO_TypeDef *enable_port;   /**< ENN output GPIO port, or NULL if unused. */
  uint16_t enable_pin;         /**< ENN pin (GPIO_PIN_x mask). */
} Stepper_Hal_Stm32_t;

/**
 * @brief Fill @p hal with callbacks bound to the given port instance.
 *
 * @param port         Port state to initialize; must stay alive for as long as
 *                     @p hal (and any ::Stepper_t initialized from it) is in use.
 * @param hal          Output HAL struct wired to this port.
 * @param step_tim     Timer whose compare output drives the STEP pin.
 * @param step_channel TIM_CHANNEL_x routed to the STEP pin (e.g. TIM2_CH2 on
 *                     PA1, TIM3_CH4 on PB1).
 * @param dir_port     DIR output GPIO port.
 * @param dir_pin      DIR pin (GPIO_PIN_x mask).
 * @param invert_dir   true when a forward step must drive DIR low instead of
 *                     high, i.e. when the motor turns its load the opposite
 *                     way to the ::Stepper_t position counter.
 * @param enable_port  ENN output GPIO port, or NULL when the enable pin is not
 *                     owned by the stepper (e.g. handled by the tmc2209 UART
 *                     driver instance); the HAL enable_write callback is then
 *                     left NULL.
 * @param enable_pin   ENN pin (GPIO_PIN_x mask); ignored when @p enable_port is
 *                     NULL.
 *
 * @note Each ::Stepper_t needs its own timer: the pulse counting interrupt is
 *       per timer, not per channel.
 * @note The ENN input drives the TMC2209 (active low, inverted here).
 */
void Stepper_Hal_Stm32_Init(Stepper_Hal_Stm32_t *port, Stepper_Hal_t *hal,
                            TIM_HandleTypeDef *step_tim, uint32_t step_channel,
                            GPIO_TypeDef *dir_port, uint16_t dir_pin,
                            bool invert_dir, GPIO_TypeDef *enable_port,
                            uint16_t enable_pin);

/** @} */ // end of stepper_port

#endif // STEPPER_HAL_H
