/* Minimal stand-in for the ST HAL header so App modules compile on the host.
 * App/Inc/motion_planner.h needs only these two handle typedefs; nothing in a
 * host test ever dereferences them. Add to this file only what a test
 * genuinely needs -- it is a seam, not a HAL reimplementation. */
#ifndef TESTS_STUB_STM32F1XX_HAL_H
#define TESTS_STUB_STM32F1XX_HAL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { int unused; } UART_HandleTypeDef;
typedef struct { int unused; } TIM_HandleTypeDef;

/* --- GPIO, for App/Src/buttons.c ------------------------------------------
 * buttons.c is the one App module that reads a pin and runs in two contexts,
 * so the seam has to model both the clock and the pin level. A test supplies
 * stub_set_tick() and stub_set_pin() to drive them; the interrupt-masking
 * primitives collapse to nothing, since a host test is single-threaded and
 * calls buttons_on_exti() directly rather than from an interrupt. */
typedef struct { int unused; } GPIO_TypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;

extern uint32_t stub_tick_ms;
extern GPIO_PinState stub_pin_state[16];

static inline uint32_t HAL_GetTick(void) { return stub_tick_ms; }

static inline GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin) {
  (void)port;
  for (unsigned bit = 0u; bit < 16u; ++bit) {
    if (pin == (uint16_t)(1u << bit)) {
      return stub_pin_state[bit];
    }
  }
  return GPIO_PIN_SET;
}

static inline uint32_t __get_PRIMASK(void) { return 0u; }
static inline void __disable_irq(void) {}
static inline void __enable_irq(void) {}

#endif /* TESTS_STUB_STM32F1XX_HAL_H */
