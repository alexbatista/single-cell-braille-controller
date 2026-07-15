// ----------------------------------------------------------------------------
// stepper_hal.h
//
// STM32 GPIO implementation of Stepper_Hal_t, using direct BSRR register
// access (CMSIS only, no ST HAL calls). This is the only stepper file that
// knows about GPIO_TypeDef; stepper.h/stepper.c stay platform independent.
//
// The pins must already be configured as push-pull outputs (done by the
// CubeMX generated code in Core/Src/gpio.c). Pin arguments are GPIO_PIN_x
// masks, so the defines from Core/Inc/main.h can be passed straight in.
// ----------------------------------------------------------------------------

#ifndef STEPPER_HAL_H
#define STEPPER_HAL_H

#include "stm32f1xx.h"

#include "stepper.h"

typedef struct Stepper_Hal_Stm32 {
  GPIO_TypeDef *step_port;
  uint16_t step_pin; // GPIO_PIN_x mask
  GPIO_TypeDef *dir_port;
  uint16_t dir_pin; // GPIO_PIN_x mask
  GPIO_TypeDef *enable_port;
  uint16_t enable_pin; //GPIO_PIN_x mask
} Stepper_Hal_Stm32_t;

// Fill hal with callbacks bound to the given port instance. port must stay
// alive for as long as the resulting hal (and any Stepper_t initialized
// from it) is in use.
//
// enable_port/enable_pin drive the TMC2209 ENN input (active low, inverted
// here). Pass NULL for enable_port when the enable pin is not owned by the
// stepper, e.g. when it is handled by the tmc2209 UART driver instance; the
// hal enable_write callback is then left NULL.
void Stepper_Hal_Stm32_Init(Stepper_Hal_Stm32_t *port, Stepper_Hal_t *hal,
                            GPIO_TypeDef *step_port, uint16_t step_pin,
                            GPIO_TypeDef *dir_port, uint16_t dir_pin,
                            GPIO_TypeDef *enable_port, uint16_t enable_pin);

#endif // STEPPER_HAL_H
