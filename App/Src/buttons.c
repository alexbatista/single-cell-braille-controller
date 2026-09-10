// ----------------------------------------------------------------------------
// buttons.c
//
// Debouncing is "first edge wins, then lock out": the press registers on the
// very first edge, and further edges within the lockout window are ignored as
// contact bounce. That gives zero perceived latency, which matters for a
// control the reader operates by feel, and it needs no polling.
//
// Each button carries one flag, not a counter. A disc move blocks for about a
// second, and a reader who taps NEXT three times during one wants to move on
// by one field, not three.
// ----------------------------------------------------------------------------

#include "buttons.h"

#include "main.h"
#include "plc_config.h"
#include "stm32f1xx_hal.h"

typedef struct {
  uint16_t pin;
  reader_event_t event;
  volatile bool pending;
  volatile uint32_t last_accepted_ms;
  volatile bool ever_accepted;
} button_t;

// Collection order is the priority order: NEXT, then PREV, then REPEAT.
static button_t buttons[] = {
    {BTN_NEXT_Pin, READER_EV_NEXT, false, 0u, false},
    {BTN_PREV_Pin, READER_EV_PREV, false, 0u, false},
    {BTN_REPEAT_Pin, READER_EV_REPEAT, false, 0u, false},
};

#define BUTTON_COUNT (sizeof buttons / sizeof buttons[0])

void buttons_init(void) {
  for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
    buttons[i].pending = false;
    buttons[i].last_accepted_ms = 0u;
    buttons[i].ever_accepted = false;
  }
}

void buttons_on_exti(uint16_t gpio_pin) {
  uint32_t now = HAL_GetTick();
  for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
    if (buttons[i].pin != gpio_pin) {
      continue;
    }
    // Unsigned subtraction, so the comparison stays correct across a wrap of
    // the millisecond counter.
    bool settled = !buttons[i].ever_accepted ||
                   (uint32_t)(now - buttons[i].last_accepted_ms) >=
                       PLC_BUTTON_DEBOUNCE_LOCKOUT_MS;
    if (settled) {
      buttons[i].pending = true;
      buttons[i].last_accepted_ms = now;
      buttons[i].ever_accepted = true;
    }
    return;
  }
}

// Read-and-clear has to be indivisible: an EXTI landing between the test and
// the clear would otherwise have its press dropped. PRIMASK is saved and
// restored rather than blindly re-enabled, so this is safe to call from a
// context that already had interrupts masked.
static bool button_take(volatile bool *pending) {
  bool taken = false;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (*pending) {
    *pending = false;
    taken = true;
  }
  if (primask == 0u) {
    __enable_irq();
  }
  return taken;
}

reader_event_t buttons_take_event(void) {
  for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
    if (button_take(&buttons[i].pending)) {
      return buttons[i].event;
    }
  }
  return READER_EV_NONE;
}
