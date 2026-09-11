// ----------------------------------------------------------------------------
// buttons.c
//
// Debouncing is "first edge wins, then wait for a real release". The press
// registers on the very first falling edge -- zero perceived latency, which
// matters for a control the reader works by feel -- and the button will not
// register again until the pin has been seen released and quiet.
//
// The release half is what a press-only lockout cannot do. These buttons have
// no hardware debounce capacitor and interrupt on the falling edge only, and a
// mechanical contact bounces when it opens as much as when it closes. So
// letting go produces further falling edges, arriving however long after the
// press the operator chose to hold. A window measured from the accepted press
// suppresses press bounce and then expires long before the release, which is
// why a single press was sometimes read as two. The hold time is the
// operator's to choose, so no fixed window can cover it: the release itself
// has to be the thing that re-arms the button.
//
// Re-arming is deliberately done from the main loop rather than the interrupt,
// because it needs to observe the pin at rest, which an edge cannot report.
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
  GPIO_TypeDef *port;
  reader_event_t event;
  volatile bool pending;
  // Stamped on every edge, bounce included, so the quiet window below is
  // measured from the last thing the contact did rather than from the press.
  volatile uint32_t last_edge_ms;
  // False from the moment a press is accepted until the pin is seen released
  // and quiet. This is what release bounce runs into.
  volatile bool armed;
} button_t;

// Collection order is the priority order: NEXT, then PREV, then REPEAT.
static button_t buttons[] = {
    {BTN_NEXT_Pin, BTN_NEXT_GPIO_Port, READER_EV_NEXT, false, 0u, true},
    {BTN_PREV_Pin, BTN_PREV_GPIO_Port, READER_EV_PREV, false, 0u, true},
    {BTN_REPEAT_Pin, BTN_REPEAT_GPIO_Port, READER_EV_REPEAT, false, 0u, true},
};

#define BUTTON_COUNT (sizeof buttons / sizeof buttons[0])

void buttons_init(void) {
  for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
    buttons[i].pending = false;
    buttons[i].last_edge_ms = 0u;
    buttons[i].armed = true;
  }
}

void buttons_on_exti(uint16_t gpio_pin) {
  uint32_t now = HAL_GetTick();
  for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
    if (buttons[i].pin != gpio_pin) {
      continue;
    }
    // Every edge counts towards the quiet window, whether or not it is
    // accepted -- that is what stops a burst of bounce from re-arming the
    // button in one of the brief gaps between its own edges.
    buttons[i].last_edge_ms = now;
    if (buttons[i].armed) {
      buttons[i].armed = false;
      buttons[i].pending = true;
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

// A button re-arms once its pin reads released and has been quiet for
// PLC_BUTTON_RELEASE_STABLE_MS. Both halves are needed: the level alone would
// re-arm during one of the brief open moments inside a bounce burst, and the
// quiet window alone would re-arm a button still being held down.
//
// The decision is taken with interrupts masked so an edge cannot land between
// reading the pin and setting the flag; without that, a press arriving in that
// gap would leave the button armed while held, and its release bounce would
// read as a second press -- the very fault this is here to prevent.
static void rearm_released_buttons(void) {
  for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
    if (buttons[i].armed) {
      continue;
    }
    if (HAL_GPIO_ReadPin(buttons[i].port, buttons[i].pin) != GPIO_PIN_SET) {
      continue; // still held down
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    // Unsigned subtraction, so this stays correct across a wrap of the
    // millisecond counter.
    if ((uint32_t)(HAL_GetTick() - buttons[i].last_edge_ms) >=
        PLC_BUTTON_RELEASE_STABLE_MS) {
      buttons[i].armed = true;
    }
    if (primask == 0u) {
      __enable_irq();
    }
  }
}

reader_event_t buttons_take_event(void) {
  rearm_released_buttons();
  for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
    if (button_take(&buttons[i].pending)) {
      return buttons[i].event;
    }
  }
  return READER_EV_NONE;
}
