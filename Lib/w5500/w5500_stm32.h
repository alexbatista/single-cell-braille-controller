/**
 * @file    w5500_stm32.h
 * @brief   STM32 SPI port for the vendored WIZnet ioLibrary.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * ioLibrary reaches hardware through callbacks the application registers.
 * This is that registration plus the four things it needs: chip select,
 * single-byte and burst SPI transfers, and the hard reset line.
 */

#ifndef W5500_STM32_H
#define W5500_STM32_H

#include "stm32f1xx_hal.h"

/**
 * @defgroup w5500_stm32 W5500 STM32 Port
 * @brief    SPI and reset glue for ioLibrary.
 * @{
 */

/**
 * @brief Register every ioLibrary callback against an SPI peripheral.
 *
 * Call once, before @c wizchip_init(). Registers chip select, single-byte
 * read/write, burst read/write, and the critical-section pair.
 *
 * @param hspi SPI peripheral wired to the W5500 (SPI2 on this board).
 */
void w5500_stm32_init(SPI_HandleTypeDef *hspi);

/**
 * @brief Pulse the W5500's reset line and wait for it to come back.
 *
 * Blocking, for about 12 ms. It runs at bring-up and on a reconnect attempt
 * -- never while the reader is mid-package -- so a timed state machine here
 * would be complexity with nothing to show for it.
 */
void w5500_stm32_hard_reset(void);

/** @} */ // end of w5500_stm32

#endif // W5500_STM32_H
