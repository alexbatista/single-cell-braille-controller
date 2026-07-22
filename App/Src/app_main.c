#include "app_main.h"
#include "main.h"
#include "braille_disc.h"
#include "motion_planner.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_uart.h"

// ----------------------------------------------------------------------------
// Demo motion: both motors turn clockwise indefinitely at one full mechanical
// revolution per 5 s (App_run() executes one revolution per call and is
// called forever from the main loop; the LED toggles once per revolution).
//

void App_init(UART_HandleTypeDef *huart_01, UART_HandleTypeDef *huart_02) {
  initialize_motors(huart_01, huart_02);
}

void App_run(void) {
  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);

  translate_char_on_disc('A');
  test_rotate_motor();
}