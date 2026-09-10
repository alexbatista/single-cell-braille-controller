/**
 * @file    fault_led.h
 * @brief   Startup fault reporting on the board LED.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * A subsystem that fails during bring-up reports its identity as a blink
 * count rather than halting, so a board with one broken peripheral still
 * starts and can be diagnosed. Codes are allocated here so two subsystems
 * cannot silently pick the same one.
 */

#ifndef FAULT_LED_H
#define FAULT_LED_H

#include <stdint.h>

/**
 * @defgroup fault_led Fault LED
 * @brief    Blink-code fault reporting shared by every subsystem.
 * @{
 */

/**
 * @name Blink codes
 *
 * One per subsystem that can fail at bring-up. Reading these apart by eye is
 * only possible while they stay small and distinct.
 * @{
 */
#define FAULT_LED_CODE_MOTOR_01 1u /**< Motor 01 driver or ZERO sensor. */
#define FAULT_LED_CODE_MOTOR_02 2u /**< Motor 02 driver or ZERO sensor. */
#define FAULT_LED_CODE_ETHERNET 3u /**< W5500 absent or not answering.  */
/** @} */

/**
 * @brief Report a fault without halting: @p blinks short blinks, a long
 *        pause, three times over, then return.
 *
 * Blocking, and deliberately so: every caller is on the startup path, where
 * nothing else needs to run. Do not call it from the main loop.
 *
 * @param blinks One of the @c FAULT_LED_CODE_* values.
 */
void fault_led_blink(uint32_t blinks);

/** @} */ // end of fault_led

#endif // FAULT_LED_H
