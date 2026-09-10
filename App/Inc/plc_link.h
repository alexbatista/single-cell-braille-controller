/**
 * @file    plc_link.h
 * @brief   MODBUS TCP client link to the PLC over the W5500.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * Owns the W5500, the one TCP connection, and the polling cycle. The device
 * is always the client: MODBUS has no way for a server to push, so "the PLC
 * sent something" is really "a background poll came back different", and
 * detecting that is this module's job.
 *
 * Everything is driven from @ref plc_link_tick and returns immediately, with
 * one exception: bringing the chip out of reset blocks for about 12 ms. That
 * happens at startup and on a reconnect attempt, never while the reader is
 * mid-package.
 */

#ifndef PLC_LINK_H
#define PLC_LINK_H

#include <stdint.h>

#include "plc_packet.h"
#include "stm32f1xx_hal.h"

/**
 * @defgroup plc_link PLC Link
 * @brief    W5500 bring-up, TCP connection and MODBUS polling.
 * @{
 */

/** @brief Where the link reports what it found. */
typedef struct {
  /** @brief A package was read successfully. */
  void (*on_snapshot)(plc_snapshot_t snapshot);
  /** @brief The link could not deliver: no chip, no cable, no answer, or a
   *         response that failed validation. */
  void (*on_fault)(void);
} plc_link_io_t;

/**
 * @brief Bring up the W5500 and start trying to reach the PLC.
 *
 * Resets the chip, registers the SPI callbacks, allocates the whole packet
 * buffer to one socket and applies the static network identity from
 * plc_config.h. A chip that does not answer is reported with
 * @ref FAULT_LED_CODE_ETHERNET and retried from @ref plc_link_tick rather
 * than halting startup.
 *
 * @param hspi SPI peripheral wired to the W5500.
 * @param io   Callbacks. Must outlive the link; not copied.
 */
void plc_link_init(SPI_HandleTypeDef *hspi, const plc_link_io_t *io);

/**
 * @brief Advance the link by one step.
 *
 * Call once per main-loop pass. Connects if not connected, polls when the
 * interval has elapsed or a poll was requested, times out a request that goes
 * unanswered, and reconnects after a fault.
 */
void plc_link_tick(void);

/**
 * @brief Ask for a poll on the next tick instead of waiting for the interval.
 *
 * Used when the reader steps past the last field and needs fresh data now.
 * Ignored while a request is already in flight.
 */
void plc_link_request_now(void);

/**
 * @brief Note that the W5500 asserted its interrupt line.
 *
 * Safe to call from interrupt context; it only sets a flag.
 *
 * @note ETH_INT (PB5) is wired and owned here, but nothing yet acts on it,
 *       and that is deliberate rather than unfinished. @ref plc_link_tick
 *       runs on every pass of the main loop, so the socket is already
 *       serviced as promptly as an interrupt could ask for -- there is no
 *       wait to short-circuit. The W5500's @c SIMR / @c Sn_IMR are left
 *       masked accordingly, so the line never asserts. This function exists
 *       so PB5 has a declared owner in the shared EXTI handler, and so an
 *       interrupt-driven or low-power revision has a place to start.
 *
 * @param gpio_pin The pin whose EXTI fired; anything else is ignored.
 */
void plc_link_on_exti(uint16_t gpio_pin);

/** @} */ // end of plc_link

#endif // PLC_LINK_H
