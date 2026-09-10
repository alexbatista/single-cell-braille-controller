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

#endif /* TESTS_STUB_STM32F1XX_HAL_H */
