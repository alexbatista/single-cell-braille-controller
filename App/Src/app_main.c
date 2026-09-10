// ----------------------------------------------------------------------------
// app_main.c
//
// The wiring layer: it owns no behaviour, only the decision about which
// modules are connected to which.
//
// In the MODBUS variant App_run() is a cooperative tick. Buttons are serviced
// first because they are the only thing a person is waiting on; the link next,
// so a poll goes out as soon as it is due; the sequencer last, since its work
// is timer-driven and a pass of delay costs nothing.
//
// Two calls inside this loop block, both bounded and both deliberate: a disc
// move takes about a second, and a buzzer pattern up to 480 ms. Button presses
// are latched by their interrupt throughout, so none is lost.
// ----------------------------------------------------------------------------

#include "app_main.h"

#include "braille_disc.h"
#include "main.h"
#include "motion_planner.h"
#include "stm32f1xx_hal.h"

#if defined(BRAILLE_INPUT_USB_CDC)

#include "usbd_cdc_if.h"

void App_init(UART_HandleTypeDef *huart_m1, UART_HandleTypeDef *huart_m2,
              TIM_HandleTypeDef *htim_m1, TIM_HandleTypeDef *htim_m2,
              TIM_HandleTypeDef *htim_buzzer, SPI_HandleTypeDef *hspi_eth) {
  (void)htim_buzzer;
  (void)hspi_eth;
  initialize_motors(huart_m1, huart_m2, htim_m1, htim_m2);
  // A disc that fails to home already blinks its motor index; the app still
  // starts, so USB stays usable for diagnosing the sensor.
  (void)calibrate_zero_position();
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

#else // BRAILLE_INPUT_MODBUS

#include "buttons.h"
#include "buzzer.h"
#include "plc_link.h"
#include "reader_ui.h"

/** @brief Timer channel the buzzer is wired to (PB6 is TIM4_CH1). */
#define APP_BUZZER_TIM_CHANNEL TIM_CHANNEL_1

// Every signature already lines up, so these tables are the whole of the
// wiring: no adapter functions are needed between the reader, the renderer,
// the buzzer and the link.
static const reader_ui_io_t app_reader_io = {
    .render_char = braille_render_char,
    .render_dots = braille_render_dots,
    .play_sound = buzzer_play,
    .request_poll = plc_link_request_now,
    .now_ms = HAL_GetTick,
};

static const plc_link_io_t app_link_io = {
    .on_snapshot = reader_ui_on_snapshot,
    .on_fault = reader_ui_on_link_fault,
};

void App_init(UART_HandleTypeDef *huart_m1, UART_HandleTypeDef *huart_m2,
              TIM_HandleTypeDef *htim_m1, TIM_HandleTypeDef *htim_m2,
              TIM_HandleTypeDef *htim_buzzer, SPI_HandleTypeDef *hspi_eth) {
  initialize_motors(huart_m1, huart_m2, htim_m1, htim_m2);
  // A disc that fails to home blinks its motor index and startup continues,
  // so a sensor fault does not cost the operator the PLC readout as well.
  (void)calibrate_zero_position();

  // The buzzer comes up before the link, so a W5500 that is missing can say
  // so out loud from inside plc_link_init().
  buzzer_init(htim_buzzer, APP_BUZZER_TIM_CHANNEL);
  buttons_init();
  reader_ui_init(&app_reader_io);
  plc_link_init(hspi_eth, &app_link_io);
}

void App_run(void) {
  reader_event_t event = buttons_take_event();
  if (event != READER_EV_NONE) {
    reader_ui_on_event(event);
  }

  // May call straight back into the reader with a snapshot or a fault, which
  // can render and therefore block. That is fine here: it is the same thread,
  // and a package arriving is exactly when the discs should move.
  plc_link_tick();

  reader_ui_tick();
}

/**
 * @brief EXTI callback for every line on this board.
 *
 * PB5 (the W5500 interrupt) and PB7/PB8/PB9 (the buttons) share
 * EXTI9_5_IRQn, so the two owners are simply offered every pin and each
 * ignores what is not theirs. Both do nothing but set a flag.
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  buttons_on_exti(GPIO_Pin);
  plc_link_on_exti(GPIO_Pin);
}

#endif // BRAILLE_INPUT_USB_CDC
