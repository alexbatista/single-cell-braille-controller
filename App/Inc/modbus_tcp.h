/**
 * @file    modbus_tcp.h
 * @brief   MODBUS TCP Read Coils request builder and response parser.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * Frame encoding and decoding only: nothing here touches a socket, which is
 * what lets every malformed-response path be exercised off-target. The device
 * is always the client, and the only function code it uses is 0x01.
 *
 * A MODBUS TCP frame is a 7-byte MBAP header -- transaction id, protocol id,
 * length, unit id -- followed by the PDU.
 */

#ifndef MODBUS_TCP_H
#define MODBUS_TCP_H

#include <stddef.h>
#include <stdint.h>

/**
 * @defgroup modbus_tcp MODBUS TCP
 * @brief    Read Coils codec for the client side.
 * @{
 */

#define MODBUS_MBAP_LEN 7u        /**< MBAP header length.                */
#define MODBUS_PROTOCOL_ID 0u     /**< Always zero for MODBUS.            */
#define MODBUS_FC_READ_COILS 0x01u /**< Function code 1.                  */
#define MODBUS_EXCEPTION_FLAG 0x80u /**< Set in the FC of an exception.   */

/** @brief Most coils one call can carry, bounded by the uint16_t result. */
#define MODBUS_MAX_COILS 16u

/** @brief Request length: MBAP + FC + start address + quantity. */
#define MODBUS_READ_COILS_REQ_LEN 12u

/** @brief Bytes a coil count occupies on the wire. */
#define MODBUS_COIL_BYTES(count) (((count) + 7u) / 8u)

/** @brief Full length of a well-formed response for @p count coils. */
#define MODBUS_READ_COILS_RSP_LEN(count)                                       \
  (MODBUS_MBAP_LEN + 2u + MODBUS_COIL_BYTES(count))

/** @brief Outcome of parsing a response. */
typedef enum {
  MODBUS_OK = 0,              /**< Valid response; bits written.          */
  MODBUS_ERR_TOO_SHORT,       /**< Fewer bytes than any legal response.   */
  MODBUS_ERR_PROTOCOL_ID,     /**< MBAP protocol id was not zero.         */
  MODBUS_ERR_TRANSACTION_ID,  /**< Reply to some other request.           */
  MODBUS_ERR_UNIT_ID,         /**< Reply from some other unit.            */
  MODBUS_ERR_LENGTH_FIELD,    /**< MBAP length disagrees with the frame.  */
  MODBUS_ERR_FUNCTION,        /**< Not a Read Coils reply.                */
  MODBUS_ERR_BYTE_COUNT,      /**< Byte count wrong for the quantity.     */
  MODBUS_ERR_EXCEPTION,       /**< Slave returned an exception.           */
  MODBUS_ERR_INVALID_ARGUMENT,/**< Caller passed null pointer or out-of-range quantity. */
} modbus_status_t;

/**
 * @brief Build a Read Coils request.
 *
 * @param out            Destination buffer.
 * @param out_capacity   Bytes available in @p out.
 * @param transaction_id Id to echo back; increment it per request so a stale
 *                       reply can be told apart from a fresh one.
 * @param unit_id        MODBUS unit id.
 * @param start_address  First coil to read.
 * @param quantity       Coils to read, 1..@ref MODBUS_MAX_COILS.
 * @return Bytes written (@ref MODBUS_READ_COILS_REQ_LEN), or 0 when the
 *         buffer is too small or @p quantity is out of range.
 */
uint16_t modbus_build_read_coils(uint8_t *out, uint16_t out_capacity,
                                 uint16_t transaction_id, uint8_t unit_id,
                                 uint16_t start_address, uint16_t quantity);

/**
 * @brief Validate a Read Coils response and extract the coil states.
 *
 * Deliberately strict. A reply that decodes cleanly but answers an earlier
 * request would hand the reader stale values with nothing to show it went
 * wrong, so the transaction id is checked as carefully as the framing. Coil
 * bits above @p quantity are masked off so padding in the last byte cannot
 * appear as extra signals.
 *
 * @param frame              Received bytes.
 * @param length             Bytes in @p frame.
 * @param transaction_id     Id of the request being answered.
 * @param unit_id            Unit id of the request being answered.
 * @param quantity           Coils requested, 1..@ref MODBUS_MAX_COILS.
 * @param out_bits           Receives the coil states, LSB = first coil.
 *                           Untouched unless @ref MODBUS_OK is returned.
 * @param out_exception_code Receives the exception code when
 *                           @ref MODBUS_ERR_EXCEPTION is returned.
 * @return @ref MODBUS_OK, or the reason the frame was rejected.
 */
modbus_status_t modbus_parse_read_coils(const uint8_t *frame, uint16_t length,
                                        uint16_t transaction_id,
                                        uint8_t unit_id, uint16_t quantity,
                                        uint16_t *out_bits,
                                        uint8_t *out_exception_code);

/** @} */ // end of modbus_tcp

#endif // MODBUS_TCP_H
