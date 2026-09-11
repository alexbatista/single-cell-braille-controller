/* Minimal stand-in for the CubeMX-generated Core/Inc/main.h.
 *
 * Core/Inc is deliberately not on the host tests' include path: pulling the
 * generated header in would drag the whole device tree behind it. App modules
 * need only the pin labels from it, and those are board facts that a test can
 * restate. Keep the values identical to Core/Inc/main.h -- a test passing
 * against a different pin map would prove nothing.
 */
#ifndef TESTS_STUB_MAIN_H
#define TESTS_STUB_MAIN_H

#include "stm32f1xx_hal.h"

#define GPIO_PIN_7 ((uint16_t)0x0080)
#define GPIO_PIN_8 ((uint16_t)0x0100)
#define GPIO_PIN_9 ((uint16_t)0x0200)

extern GPIO_TypeDef *const stub_gpiob;
#define GPIOB stub_gpiob

#define BTN_REPEAT_Pin GPIO_PIN_7
#define BTN_REPEAT_GPIO_Port GPIOB
#define BTN_NEXT_Pin GPIO_PIN_8
#define BTN_NEXT_GPIO_Port GPIOB
#define BTN_PREV_Pin GPIO_PIN_9
#define BTN_PREV_GPIO_Port GPIOB

#endif /* TESTS_STUB_MAIN_H */
