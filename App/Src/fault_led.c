// ----------------------------------------------------------------------------
// fault_led.c
//
// The blink-code reporter shared by every subsystem that can fail during
// bring-up. It lived in motion_planner.c while the motors were the only
// callers; the Ethernet link is the third, so it moved here instead of being
// copied.
// ----------------------------------------------------------------------------

#include "fault_led.h"
#include "main.h"
#include "stm32f1xx_hal.h"

// Shape of a blink code: <code> short blinks, a long gap, repeated.
#define FAULT_LED_BURSTS 3u
#define FAULT_LED_ON_MS 100u
#define FAULT_LED_OFF_MS 200u
#define FAULT_LED_GAP_MS 800u

void fault_led_blink(uint32_t blinks) {
  for (uint32_t burst = 0u; burst < FAULT_LED_BURSTS; ++burst) {
    for (uint32_t i = 0u; i < blinks; ++i) {
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
      HAL_Delay(FAULT_LED_ON_MS);
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
      HAL_Delay(FAULT_LED_OFF_MS);
    }
    HAL_Delay(FAULT_LED_GAP_MS);
  }
}
