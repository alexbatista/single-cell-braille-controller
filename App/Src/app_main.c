#include "app_main.h"
#include "main.h"
#include "braille_disc.h"
#include "motion_planner.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_uart.h"
#include "usbd_cdc_if.h"

// ----------------------------------------------------------------------------
// Demo motion: both motors turn clockwise indefinitely at one full mechanical
// revolution per 5 s (App_run() executes one revolution per call and is
// called forever from the main loop; the LED toggles once per revolution).
//

void App_init(UART_HandleTypeDef *huart_01, UART_HandleTypeDef *huart_02,
              TIM_HandleTypeDef *htim_01, TIM_HandleTypeDef *htim_02) {
  initialize_motors(huart_01, huart_02, htim_01, htim_02);
  calibrate_zero_position();
}

void App_run(void) {
  uint8_t c; // One byte character buffer for user input

  if (!CDC_ReadChar(&c)) {
    return;
  }

  // Echo the byte straight back so the host terminal shows what arrived. This
  // proves both USB directions before any motion is involved.
  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
  CDC_Transmit_FS(&c, 1u);

  // Once the echo is confirmed, drive the disc from the received character:
  translate_char_on_disc(c);
}