#include "stepper_hal.h"

#include <stddef.h>

static void gpio_write(GPIO_TypeDef *gpio_port, uint16_t pin_mask,
                       bool level) {
  if (level) {
    gpio_port->BSRR = pin_mask;
  } else {
    gpio_port->BSRR = (uint32_t)pin_mask << 16u;
  }
}

static void step_write(void *ctx, bool level) {
  Stepper_Hal_Stm32_t *port = ctx;
  gpio_write(port->step_port, port->step_pin, level);
}

static void dir_write(void *ctx, bool level) {
  Stepper_Hal_Stm32_t *port = ctx;
  gpio_write(port->dir_port, port->dir_pin, level);
}

static void enable_write(void *ctx, bool enable) {
  Stepper_Hal_Stm32_t *port = ctx;
  // The TMC2209 ENN input is active low: drive the pin low to enable
  gpio_write(port->enable_port, port->enable_pin, !enable);
}

void Stepper_Hal_Stm32_Init(Stepper_Hal_Stm32_t *port, Stepper_Hal_t *hal,
                            GPIO_TypeDef *step_port, uint16_t step_pin,
                            GPIO_TypeDef *dir_port, uint16_t dir_pin,
                            GPIO_TypeDef *enable_port, uint16_t enable_pin) {
  port->step_port = step_port;
  port->step_pin = step_pin;
  port->dir_port = dir_port;
  port->dir_pin = dir_pin;
  port->enable_port = enable_port;
  port->enable_pin = enable_pin;

  hal->step_write = step_write;
  hal->dir_write = dir_write;
  hal->enable_write = (enable_port != NULL) ? enable_write : NULL;
  hal->ctx = port;
}
