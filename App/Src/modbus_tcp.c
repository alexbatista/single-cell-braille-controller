// ----------------------------------------------------------------------------
// modbus_tcp.c
//
// Read Coils framing for the client side. No sockets here on purpose: the
// caller owns the connection, so every rejection path in the parser can be
// exercised from a host test with a hand-built byte array.
// ----------------------------------------------------------------------------

#include "modbus_tcp.h"

// MBAP field offsets, shared by requests and responses.
#define MB_OFF_TRANSACTION_HI 0u
#define MB_OFF_TRANSACTION_LO 1u
#define MB_OFF_PROTOCOL_HI 2u
#define MB_OFF_PROTOCOL_LO 3u
#define MB_OFF_LENGTH_HI 4u
#define MB_OFF_LENGTH_LO 5u
#define MB_OFF_UNIT 6u
#define MB_OFF_FUNCTION 7u

// Request PDU offsets, after the function code.
#define MB_OFF_REQ_ADDRESS_HI 8u
#define MB_OFF_REQ_ADDRESS_LO 9u
#define MB_OFF_REQ_QUANTITY_HI 10u
#define MB_OFF_REQ_QUANTITY_LO 11u

// Response PDU offsets.
#define MB_OFF_RSP_BYTE_COUNT 8u
#define MB_OFF_RSP_DATA 9u
#define MB_OFF_RSP_EXCEPTION_CODE 8u

// The MBAP length field counts everything after it: unit id onwards.
#define MB_LENGTH_FIELD_OF(pdu_bytes) ((pdu_bytes) + 1u)
#define MB_REQ_PDU_BYTES 5u /* FC + address + quantity */
#define MB_EXCEPTION_RSP_LEN 9u
#define MB_BITS_PER_BYTE 8u

static uint16_t be16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << MB_BITS_PER_BYTE) | (uint16_t)p[1]);
}

uint16_t modbus_build_read_coils(uint8_t *out, uint16_t out_capacity,
                                 uint16_t transaction_id, uint8_t unit_id,
                                 uint16_t start_address, uint16_t quantity) {
  if (out == NULL || out_capacity < MODBUS_READ_COILS_REQ_LEN) {
    return 0u;
  }
  if (quantity == 0u || quantity > MODBUS_MAX_COILS) {
    return 0u;
  }

  out[MB_OFF_TRANSACTION_HI] = (uint8_t)(transaction_id >> MB_BITS_PER_BYTE);
  out[MB_OFF_TRANSACTION_LO] = (uint8_t)(transaction_id & 0xFFu);
  out[MB_OFF_PROTOCOL_HI] = (uint8_t)(MODBUS_PROTOCOL_ID >> MB_BITS_PER_BYTE);
  out[MB_OFF_PROTOCOL_LO] = (uint8_t)(MODBUS_PROTOCOL_ID & 0xFFu);
  out[MB_OFF_LENGTH_HI] = 0u;
  out[MB_OFF_LENGTH_LO] = (uint8_t)MB_LENGTH_FIELD_OF(MB_REQ_PDU_BYTES);
  out[MB_OFF_UNIT] = unit_id;
  out[MB_OFF_FUNCTION] = MODBUS_FC_READ_COILS;
  out[MB_OFF_REQ_ADDRESS_HI] = (uint8_t)(start_address >> MB_BITS_PER_BYTE);
  out[MB_OFF_REQ_ADDRESS_LO] = (uint8_t)(start_address & 0xFFu);
  out[MB_OFF_REQ_QUANTITY_HI] = (uint8_t)(quantity >> MB_BITS_PER_BYTE);
  out[MB_OFF_REQ_QUANTITY_LO] = (uint8_t)(quantity & 0xFFu);

  return MODBUS_READ_COILS_REQ_LEN;
}

modbus_status_t modbus_parse_read_coils(const uint8_t *frame, uint16_t length,
                                        uint16_t transaction_id,
                                        uint8_t unit_id, uint16_t quantity,
                                        uint16_t *out_bits,
                                        uint8_t *out_exception_code) {
  if (frame == NULL || out_bits == NULL || quantity == 0u ||
      quantity > MODBUS_MAX_COILS) {
    return MODBUS_ERR_TOO_SHORT;
  }
  // An exception reply is the shortest legal response, so anything below it
  // cannot be parsed at all.
  if (length < MB_EXCEPTION_RSP_LEN) {
    return MODBUS_ERR_TOO_SHORT;
  }
  if (be16(&frame[MB_OFF_PROTOCOL_HI]) != MODBUS_PROTOCOL_ID) {
    return MODBUS_ERR_PROTOCOL_ID;
  }
  if (be16(&frame[MB_OFF_TRANSACTION_HI]) != transaction_id) {
    return MODBUS_ERR_TRANSACTION_ID;
  }
  if (frame[MB_OFF_UNIT] != unit_id) {
    return MODBUS_ERR_UNIT_ID;
  }
  // The MBAP length field counts unit id onwards; if it disagrees with what
  // actually arrived, the framing is untrustworthy however well the rest
  // decodes.
  if (be16(&frame[MB_OFF_LENGTH_HI]) != (uint16_t)(length - MB_OFF_UNIT)) {
    return MODBUS_ERR_LENGTH_FIELD;
  }

  if (frame[MB_OFF_FUNCTION] ==
      (MODBUS_FC_READ_COILS | MODBUS_EXCEPTION_FLAG)) {
    if (out_exception_code != NULL) {
      *out_exception_code = frame[MB_OFF_RSP_EXCEPTION_CODE];
    }
    return MODBUS_ERR_EXCEPTION;
  }
  if (frame[MB_OFF_FUNCTION] != MODBUS_FC_READ_COILS) {
    return MODBUS_ERR_FUNCTION;
  }

  uint16_t expected_bytes = MODBUS_COIL_BYTES(quantity);
  if (frame[MB_OFF_RSP_BYTE_COUNT] != (uint8_t)expected_bytes) {
    return MODBUS_ERR_BYTE_COUNT;
  }
  if (length < MODBUS_READ_COILS_RSP_LEN(quantity)) {
    return MODBUS_ERR_TOO_SHORT;
  }

  uint16_t bits = 0u;
  for (uint16_t i = 0u; i < expected_bytes; ++i) {
    bits |= (uint16_t)((uint16_t)frame[MB_OFF_RSP_DATA + i]
                       << (i * MB_BITS_PER_BYTE));
  }
  // Padding in the final byte is not data; masking it off stops a slave that
  // sets those bits from inventing signals we never asked for.
  if (quantity < MODBUS_MAX_COILS) {
    bits &= (uint16_t)((1u << quantity) - 1u);
  }
  *out_bits = bits;
  return MODBUS_OK;
}
