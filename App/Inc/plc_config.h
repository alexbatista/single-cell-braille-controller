/**
 * @file    plc_config.h
 * @brief   Deployment settings for the MODBUS TCP link and the reader.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * Everything an installation might need to change lives here, so no numeric
 * literal appears at a use site. Editing this file and reflashing is the
 * supported way to move the device to another network or PLC; there is no
 * runtime configuration.
 */

#ifndef PLC_CONFIG_H
#define PLC_CONFIG_H

#include <stdint.h>

/**
 * @defgroup plc_config PLC Configuration
 * @brief    Network identity, PLC endpoint, packet range and timings.
 * @{
 */

/**
 * @name This device's network identity (static; there is no DHCP client)
 * @{
 */
#define PLC_DEVICE_MAC_0 0x00u
#define PLC_DEVICE_MAC_1 0x08u
#define PLC_DEVICE_MAC_2 0xDCu
#define PLC_DEVICE_MAC_3 0x11u
#define PLC_DEVICE_MAC_4 0x22u
#define PLC_DEVICE_MAC_5 0x33u

#define PLC_DEVICE_IP_0 10u
#define PLC_DEVICE_IP_1 0u
#define PLC_DEVICE_IP_2 0u
#define PLC_DEVICE_IP_3 50u

#define PLC_DEVICE_MASK_0 255u
#define PLC_DEVICE_MASK_1 255u
#define PLC_DEVICE_MASK_2 255u
#define PLC_DEVICE_MASK_3 0u

#define PLC_GATEWAY_0 10u
#define PLC_GATEWAY_1 0u
#define PLC_GATEWAY_2 0u
#define PLC_GATEWAY_3 1u
/** @} */

/**
 * @name The PLC endpoint and the package it serves
 * @{
 */
#define PLC_SERVER_IP_0 10u
#define PLC_SERVER_IP_1 0u
#define PLC_SERVER_IP_2 0u
#define PLC_SERVER_IP_3 204u

#define PLC_SERVER_PORT 503u  /**< Not the 502 default; this PLC uses 503. */
#define PLC_LOCAL_PORT 50000u /**< Our ephemeral source port.              */
#define PLC_UNIT_ID 1u        /**< MODBUS unit/slave id.                   */

#define PLC_COIL_BASE_ADDRESS 0u /**< First coil of the package.           */
#define PLC_COIL_COUNT 11u       /**< Coils in one package.                */
#define PLC_SOCKET_NUMBER 0u     /**< W5500 socket used for the link.      */
/** @} */

/**
 * @name Reader timings
 * @{
 */
/** How long a label glyph is held before the next glyph replaces it. Raising
 *  this makes a full 11-field read proportionally slower; at 1500 ms a read is
 *  roughly 45 s. */
#define PLC_LABEL_DWELL_MS 1500u
/** @} */

/**
 * @name Link timings
 * @{
 */
#define PLC_POLL_INTERVAL_MS 500u     /**< Gap between background polls.   */
#define PLC_RESPONSE_TIMEOUT_MS 1000u /**< Reply deadline for one request. */
#define PLC_RECONNECT_DELAY_MS 2000u  /**< Pause before retrying a link.   */
#define PLC_W5500_RESET_LOW_MS 2u     /**< RSTn hold; datasheet min 500us. */
#define PLC_W5500_BOOT_MS 10u         /**< PLL settle after RSTn release.  */
/** @} */

/**
 * @name Rate limits
 *
 * Without these, a chattering signal or an unplugged cable would make the
 * buzzer sound continuously and drown out the sounds that matter.
 * @{
 */
#define PLC_QUEUED_SOUND_MIN_INTERVAL_MS 5000u
#define PLC_FAULT_SOUND_MIN_INTERVAL_MS 10000u
/** @} */

/** @brief Debounce lockout: edges within this window are one press. */
#define PLC_BUTTON_DEBOUNCE_LOCKOUT_MS 40u

/** @} */ // end of plc_config

#endif // PLC_CONFIG_H
