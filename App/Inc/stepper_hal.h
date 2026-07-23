// ----------------------------------------------------------------------------
// stepper_hal.h
//
// STM32 implementation of Stepper_Hal_t. STEP pulses come from a timer PWM
// output; DIR and ENN are plain GPIOs written through BSRR. This is the only
// stepper file that knows about GPIO_TypeDef and TIM_HandleTypeDef;
// stepper.h/stepper.c stay platform independent.
//
// The DIR/ENN pins must already be configured as push-pull outputs and the
// STEP timer must already be initialized in PWM mode with its channel
// configured and its global interrupt enabled (all done by the CubeMX
// generated code in Core/Src/gpio.c and Core/Src/tim.c). Pin arguments are
// GPIO_PIN_x masks, so the defines from Core/Inc/main.h can be passed
// straight in.
//
// The STEP pin itself is not passed here: MX_TIMx_Init() already puts it in
// alternate function mode through HAL_TIM_MspPostInit(), and from then on it
// belongs to the timer, not to this port.
// ----------------------------------------------------------------------------

#ifndef STEPPER_HAL_H
#define STEPPER_HAL_H

#include "stm32f1xx_hal.h"

#include "stepper.h"

typedef struct Stepper_Hal_Stm32 {
  TIM_HandleTypeDef *step_tim; // timer whose PWM output drives the STEP pin
  uint32_t step_channel;       // TIM_CHANNEL_x routed to the STEP pin
  GPIO_TypeDef *dir_port;
  uint16_t dir_pin; // GPIO_PIN_x mask
  GPIO_TypeDef *enable_port;
  uint16_t enable_pin; // GPIO_PIN_x mask
} Stepper_Hal_Stm32_t;

// Fill hal with callbacks bound to the given port instance. port must stay
// alive for as long as the resulting hal (and any Stepper_t initialized
// from it) is in use.
//
// step_tim/step_channel select the compare output routed to the STEP pin
// (TIM2_CH2 on PA1, TIM3_CH4 on PB1). Each Stepper_t needs its own timer:
// the pulse counting interrupt is per timer, not per channel.
//
// enable_port/enable_pin drive the TMC2209 ENN input (active low, inverted
// here). Pass NULL for enable_port when the enable pin is not owned by the
// stepper, e.g. when it is handled by the tmc2209 UART driver instance; the
// hal enable_write callback is then left NULL.
void Stepper_Hal_Stm32_Init(Stepper_Hal_Stm32_t *port, Stepper_Hal_t *hal,
                            TIM_HandleTypeDef *step_tim, uint32_t step_channel,
                            GPIO_TypeDef *dir_port, uint16_t dir_pin,
                            GPIO_TypeDef *enable_port, uint16_t enable_pin);

#endif // STEPPER_HAL_H
