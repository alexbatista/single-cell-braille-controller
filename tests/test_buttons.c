/* Host tests for App/Src/buttons.c -- the debounce, and specifically the
 * release bounce that a press-only lockout cannot see.
 *
 * The buttons are wired active-low with a pull-up and their EXTI fires on the
 * falling edge only. A mechanical contact bounces when it opens as well as
 * when it closes, so releasing the button produces further falling edges --
 * and those arrive however long after the press the operator held it, which is
 * a time the firmware does not get to choose. A lockout measured from the
 * accepted press therefore suppresses press bounce and lets release bounce
 * straight through as a second press.
 *
 * These tests model the pin level as well as the edges, because a debounce
 * that only sees edges cannot tell the two cases apart.
 */
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>

#include "main.h"          /* stub: the board's pin labels */
#include "plc_config.h"
#include "stm32f1xx_hal.h" /* stub: tick, pin reads, masking primitives */

/* The seam the stub HAL reads. */
uint32_t stub_tick_ms;
GPIO_PinState stub_pin_state[16];
static GPIO_TypeDef stub_gpiob_obj;
GPIO_TypeDef *const stub_gpiob = &stub_gpiob_obj;

#include "buttons.c"

static uint8_t bit_of(uint16_t pin) {
  for (uint8_t b = 0u; b < 16u; ++b) {
    if (pin == (uint16_t)(1u << b)) {
      return b;
    }
  }
  return 0u;
}

/* held == the contact is closed, which pulls the active-low pin down. */
static void hold(uint16_t pin, bool held) {
  stub_pin_state[bit_of(pin)] = held ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
static void advance(uint32_t ms) { stub_tick_ms += ms; }

/* A falling edge only happens when the contact closes. */
static void close_contact(uint16_t pin) {
  hold(pin, true);
  buttons_on_exti(pin);
}

/* One clean press: contact closes, stays closed, then opens for good. */
static void clean_press(uint16_t pin, uint32_t held_ms) {
  close_contact(pin);
  advance(held_ms);
  hold(pin, false);
}

/* Let the main loop run for a while, returning the first event it collected.
 * Re-arming happens here, so a test that never calls this never re-arms --
 * exactly like a firmware whose loop is blocked. */
static reader_event_t run_loop(uint32_t ms) {
  reader_event_t first = READER_EV_NONE;
  for (uint32_t t = 0u; t < ms; ++t) {
    reader_event_t e = buttons_take_event();
    if (e != READER_EV_NONE && first == READER_EV_NONE) {
      first = e;
    }
    advance(1u);
  }
  return first;
}

static void reset_all(void) {
  stub_tick_ms = 1000u;
  for (uint8_t b = 0u; b < 16u; ++b) {
    stub_pin_state[b] = GPIO_PIN_SET; /* released */
  }
  buttons_init();
}

int main(void) {
  /* -- a clean press is one event -- */
  reset_all();
  close_contact(BTN_NEXT_Pin);
  CHECK_EQ(buttons_take_event(), READER_EV_NEXT);
  CHECK_EQ(buttons_take_event(), READER_EV_NONE);

  /* -- bounce while the contact closes is still one event -- */
  reset_all();
  close_contact(BTN_NEXT_Pin);
  for (int i = 0; i < 6; ++i) { /* contact chatters open/closed */
    advance(1u);
    hold(BTN_NEXT_Pin, false);
    advance(1u);
    close_contact(BTN_NEXT_Pin);
  }
  CHECK_EQ(buttons_take_event(), READER_EV_NEXT);
  CHECK_EQ(buttons_take_event(), READER_EV_NONE);

  /* -- THE BUG: bounce while the contact opens must not read as a press --
   * The hold is 300 ms, far past any press-lockout window, which is the point:
   * how long the operator holds the button is not something the firmware
   * chooses, so a lockout measured from the press cannot cover the release. */
  reset_all();
  close_contact(BTN_NEXT_Pin);
  CHECK_EQ(buttons_take_event(), READER_EV_NEXT);
  advance(300u);
  hold(BTN_NEXT_Pin, false); /* operator lets go */
  for (int i = 0; i < 4; ++i) {
    advance(1u);
    close_contact(BTN_NEXT_Pin); /* contact slaps shut again: falling edge */
    advance(1u);
    hold(BTN_NEXT_Pin, false);
  }
  CHECK_EQ(run_loop(200u), READER_EV_NONE);

  /* -- a genuine second press, after a real release, is accepted -- */
  reset_all();
  clean_press(BTN_NEXT_Pin, 50u);
  CHECK_EQ(buttons_take_event(), READER_EV_NEXT);
  CHECK_EQ(run_loop(PLC_BUTTON_RELEASE_STABLE_MS * 2u), READER_EV_NONE);
  close_contact(BTN_NEXT_Pin);
  CHECK_EQ(buttons_take_event(), READER_EV_NEXT);

  /* -- holding the button down never re-arms it -- */
  reset_all();
  close_contact(BTN_NEXT_Pin);
  CHECK_EQ(buttons_take_event(), READER_EV_NEXT);
  for (int i = 0; i < 5; ++i) {
    advance(500u);
    buttons_on_exti(BTN_NEXT_Pin); /* spurious edge while still held */
  }
  CHECK_EQ(run_loop(100u), READER_EV_NONE);

  /* -- collection order is NEXT, then PREV, then REPEAT -- */
  reset_all();
  close_contact(BTN_REPEAT_Pin);
  close_contact(BTN_PREV_Pin);
  close_contact(BTN_NEXT_Pin);
  CHECK_EQ(buttons_take_event(), READER_EV_NEXT);
  CHECK_EQ(buttons_take_event(), READER_EV_PREV);
  CHECK_EQ(buttons_take_event(), READER_EV_REPEAT);
  CHECK_EQ(buttons_take_event(), READER_EV_NONE);

  /* -- a pin that is not a button is ignored, not misattributed -- */
  reset_all();
  buttons_on_exti((uint16_t)0x0020); /* ETH_INT, PB5 */
  CHECK_EQ(buttons_take_event(), READER_EV_NONE);

  /* -- repeated presses before a drain collapse into one event -- */
  /* A disc move blocks the loop for about a second; an operator tapping NEXT
   * during one means "move on", not "skip three fields". */
  reset_all();
  clean_press(BTN_NEXT_Pin, 20u);
  advance(PLC_BUTTON_RELEASE_STABLE_MS * 2u);
  /* no drain in between, so no re-arm: the second close is swallowed */
  close_contact(BTN_NEXT_Pin);
  CHECK_EQ(buttons_take_event(), READER_EV_NEXT);
  CHECK_EQ(buttons_take_event(), READER_EV_NONE);

  TESTS_REPORT("buttons");
}
