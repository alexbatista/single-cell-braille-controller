/**
 * @file    buttons.h
 * @brief   Debounced NEXT / PREV / REPEAT presses.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * The three buttons share one EXTI line group, so the interrupt only records
 * that a press happened and the main loop collects it. Each button holds a
 * single flag rather than a count, which is what makes an impatient reader's
 * repeated taps during a disc move collapse into one advance instead of
 * skipping fields.
 */

#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

#include "reader_ui.h"

/**
 * @defgroup buttons Buttons
 * @brief    Debounced navigation input.
 * @{
 */

/** @brief Clear all pending presses and debounce state. */
void buttons_init(void);

/**
 * @brief Record a press from the EXTI callback.
 *
 * Safe to call from interrupt context, and cheap: it compares one timestamp
 * and sets one flag. Pins that do not belong to a button are ignored, so the
 * shared handler can hand it every line.
 *
 * @param gpio_pin The pin whose EXTI fired.
 */
void buttons_on_exti(uint16_t gpio_pin);

/**
 * @brief Take the next pending press, if any.
 *
 * Call from the main loop. When more than one button is pending, NEXT wins,
 * then PREV, then REPEAT -- the reader is far more likely to be moving
 * forward than to have meant a simultaneous press.
 *
 * @return The press, or @c READER_EV_NONE when nothing is pending.
 */
reader_event_t buttons_take_event(void);

/** @} */ // end of buttons

#endif // BUTTONS_H
