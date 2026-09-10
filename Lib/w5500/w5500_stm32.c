// ----------------------------------------------------------------------------
// w5500_stm32.c
//
// ioLibrary calls out through function pointers; this file supplies them for
// SPI2 with a software-driven chip select on PB12.
//
// Burst callbacks are registered as well as the single-byte pair, because
// every register access ioLibrary makes is a burst underneath: without them
// each byte of a 16-byte read would be its own HAL call. Confirmed against
// the fetched wizchip_conf.h that reg_wizchip_spiburst_cbfunc exists for the
// W5500 and takes (uint8_t*, uint16_t) -- see Lib/w5500/UPSTREAM.md.
// ----------------------------------------------------------------------------

#include "w5500_stm32.h"

#include "main.h"
#include "plc_config.h"
#include "wizchip_conf.h"

/** @brief Clocked out while reading, since the bus is full duplex. */
#define W5500_SPI_DUMMY_BYTE 0xFFu

/**
 * @brief Per-transfer SPI timeout.
 *
 * At 12 Mbit/s the largest transfer this driver makes is far under a
 * millisecond, so anything approaching this bound means the bus is stuck
 * rather than busy.
 */
#define W5500_SPI_TIMEOUT_MS 100u

static SPI_HandleTypeDef *w5500_spi;

static void w5500_cs_select(void) {
  HAL_GPIO_WritePin(ETH_NSS_GPIO_Port, ETH_NSS_Pin, GPIO_PIN_RESET);
}

static void w5500_cs_deselect(void) {
  HAL_GPIO_WritePin(ETH_NSS_GPIO_Port, ETH_NSS_Pin, GPIO_PIN_SET);
}

static void w5500_write_burst(uint8_t *buffer, uint16_t length) {
  (void)HAL_SPI_Transmit(w5500_spi, buffer, length, W5500_SPI_TIMEOUT_MS);
}

static void w5500_read_burst(uint8_t *buffer, uint16_t length) {
  // Full duplex: bytes only come back if something is clocked out. Filling
  // the buffer with the dummy byte and using it as both source and
  // destination is what HAL_SPI_Receive does internally on this family,
  // written out here so the dummy traffic is visible rather than implied.
  for (uint16_t i = 0u; i < length; ++i) {
    buffer[i] = W5500_SPI_DUMMY_BYTE;
  }
  (void)HAL_SPI_TransmitReceive(w5500_spi, buffer, buffer, length,
                                W5500_SPI_TIMEOUT_MS);
}

static void w5500_write_byte(uint8_t data) { w5500_write_burst(&data, 1u); }

static uint8_t w5500_read_byte(void) {
  uint8_t data = W5500_SPI_DUMMY_BYTE;
  w5500_read_burst(&data, 1u);
  return data;
}

// ioLibrary wraps each chip access in these so an application that touches
// SPI from more than one context can serialise it. Here SPI2 is driven only
// from plc_link_tick(), which runs in the main loop; no interrupt on this
// board touches the bus. Masking interrupts anyway would add jitter to the
// stepper's STEP timers for no benefit.
static void w5500_cris_enter(void) {}
static void w5500_cris_exit(void) {}

void w5500_stm32_init(SPI_HandleTypeDef *hspi) {
  w5500_spi = hspi;
  w5500_cs_deselect();

  reg_wizchip_cris_cbfunc(w5500_cris_enter, w5500_cris_exit);
  reg_wizchip_cs_cbfunc(w5500_cs_select, w5500_cs_deselect);
  reg_wizchip_spi_cbfunc(w5500_read_byte, w5500_write_byte);
  reg_wizchip_spiburst_cbfunc(w5500_read_burst, w5500_write_burst);
}

void w5500_stm32_hard_reset(void) {
  HAL_GPIO_WritePin(ETH_RESET_GPIO_Port, ETH_RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(PLC_W5500_RESET_LOW_MS);
  HAL_GPIO_WritePin(ETH_RESET_GPIO_Port, ETH_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(PLC_W5500_BOOT_MS);
}
