/* Task 5 -- the MODBUS TCP codec. The parser's job is to be suspicious: a
 * response that decodes cleanly but belongs to an earlier request would show
 * the reader stale values with no sign anything was wrong, so the stale
 * transaction-id case matters as much as the malformed ones. */
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "modbus_tcp.c"

#define TID 0x1234u
#define UNIT 1u
#define ADDR 0u
#define QTY 11u

/* A well-formed 11-coil response carrying the given two data bytes. */
static uint16_t make_response(uint8_t *f, uint16_t tid, uint8_t unit,
                              uint8_t fc, uint8_t byte_count, uint8_t d0,
                              uint8_t d1) {
  f[0] = (uint8_t)(tid >> 8);
  f[1] = (uint8_t)(tid & 0xFFu);
  f[2] = 0u;
  f[3] = 0u;
  f[4] = 0u;
  f[5] = (uint8_t)(3u + byte_count); /* unit + fc + count + data */
  f[6] = unit;
  f[7] = fc;
  f[8] = byte_count;
  f[9] = d0;
  f[10] = d1;
  return (uint16_t)(9u + byte_count);
}

int main(void) {
  uint8_t req[MODBUS_READ_COILS_REQ_LEN];
  uint16_t bits = 0xFFFFu;
  uint8_t exc = 0xFFu;

  /* ---- build ---- */
  CHECK_EQ(modbus_build_read_coils(req, sizeof req, TID, UNIT, ADDR, QTY),
           MODBUS_READ_COILS_REQ_LEN);
  CHECK_EQ(req[0], 0x12u);              /* transaction id hi */
  CHECK_EQ(req[1], 0x34u);              /* transaction id lo */
  CHECK_EQ(req[2], 0x00u);              /* protocol id hi    */
  CHECK_EQ(req[3], 0x00u);              /* protocol id lo    */
  CHECK_EQ(req[4], 0x00u);              /* length hi         */
  CHECK_EQ(req[5], 0x06u);              /* length lo         */
  CHECK_EQ(req[6], UNIT);               /* unit id           */
  CHECK_EQ(req[7], MODBUS_FC_READ_COILS);
  CHECK_EQ(req[8], 0x00u);              /* start address hi  */
  CHECK_EQ(req[9], 0x00u);              /* start address lo  */
  CHECK_EQ(req[10], 0x00u);             /* quantity hi       */
  CHECK_EQ(req[11], 0x0Bu);             /* quantity lo = 11  */

  /* Too small a buffer is refused rather than truncated. */
  CHECK_EQ(modbus_build_read_coils(req, MODBUS_READ_COILS_REQ_LEN - 1u, TID,
                                   UNIT, ADDR, QTY),
           0u);
  /* Quantities the uint16_t snapshot cannot hold are refused. */
  CHECK_EQ(modbus_build_read_coils(req, sizeof req, TID, UNIT, ADDR,
                                   MODBUS_MAX_COILS + 1u),
           0u);
  CHECK_EQ(modbus_build_read_coils(req, sizeof req, TID, UNIT, ADDR, 0u), 0u);

  /* ---- parse: the happy path ---- */
  uint8_t f[32];
  uint16_t n = make_response(f, TID, UNIT, MODBUS_FC_READ_COILS, 2u, 0x15u,
                             0x05u);
  CHECK_EQ(n, MODBUS_READ_COILS_RSP_LEN(QTY));
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_OK);
  /* 0x15 = coils 0,2,4 ; 0x05 = coils 8,10 */
  CHECK_EQ(bits, 0x0515u);

  /* Every coil individually, to prove the bit order. */
  for (uint8_t i = 0u; i < QTY; ++i) {
    uint8_t d0 = (i < 8u) ? (uint8_t)(1u << i) : 0u;
    uint8_t d1 = (i >= 8u) ? (uint8_t)(1u << (i - 8u)) : 0u;
    n = make_response(f, TID, UNIT, MODBUS_FC_READ_COILS, 2u, d0, d1);
    CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
             MODBUS_OK);
    CHECK_EQ(bits, (uint16_t)(1u << i));
  }

  /* Bits above the requested quantity are masked off, so a slave that pads
   * the last byte cannot inject phantom signals. */
  n = make_response(f, TID, UNIT, MODBUS_FC_READ_COILS, 2u, 0xFFu, 0xFFu);
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_OK);
  CHECK_EQ(bits, 0x07FFu);

  /* ---- parse: rejections ---- */
  n = make_response(f, TID, UNIT, MODBUS_FC_READ_COILS, 2u, 0u, 0u);

  CHECK_EQ(modbus_parse_read_coils(f, (uint16_t)(MODBUS_MBAP_LEN + 1u), TID,
                                   UNIT, QTY, &bits, &exc),
           MODBUS_ERR_TOO_SHORT);

  f[2] = 0x01u; /* non-zero protocol id */
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_PROTOCOL_ID);
  f[2] = 0x00u;

  CHECK_EQ(modbus_parse_read_coils(f, n, (uint16_t)(TID + 1u), UNIT, QTY,
                                   &bits, &exc),
           MODBUS_ERR_TRANSACTION_ID);

  CHECK_EQ(modbus_parse_read_coils(f, n, TID, (uint8_t)(UNIT + 1u), QTY, &bits,
                                   &exc),
           MODBUS_ERR_UNIT_ID);

  f[5] = 0x09u; /* MBAP length field disagrees with the frame */
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_LENGTH_FIELD);
  f[5] = 0x05u;

  n = make_response(f, TID, UNIT, MODBUS_FC_READ_COILS, 3u, 0u, 0u);
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_BYTE_COUNT);

  n = make_response(f, TID, UNIT, 0x03u, 2u, 0u, 0u); /* Read Holding Regs */
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_FUNCTION);

  /* An exception response is 9 bytes: MBAP, FC|0x80, exception code. */
  f[0] = (uint8_t)(TID >> 8);
  f[1] = (uint8_t)(TID & 0xFFu);
  f[2] = 0u; f[3] = 0u; f[4] = 0u; f[5] = 0x03u;
  f[6] = UNIT;
  f[7] = MODBUS_FC_READ_COILS | MODBUS_EXCEPTION_FLAG;
  f[8] = 0x02u; /* ILLEGAL DATA ADDRESS */
  exc = 0u;
  CHECK_EQ(modbus_parse_read_coils(f, 9u, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_EXCEPTION);
  CHECK_EQ(exc, 0x02u);

  /* ---- parse: invalid arguments ---- */
  n = make_response(f, TID, UNIT, MODBUS_FC_READ_COILS, 2u, 0u, 0u);

  CHECK_EQ(modbus_parse_read_coils(NULL, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_INVALID_ARGUMENT);

  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, NULL, &exc),
           MODBUS_ERR_INVALID_ARGUMENT);

  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, 0u, &bits, &exc),
           MODBUS_ERR_INVALID_ARGUMENT);

  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, MODBUS_MAX_COILS + 1u,
                                   &bits, &exc),
           MODBUS_ERR_INVALID_ARGUMENT);

  /* Truncated frame after byte-count validation: a frame promising 2 data
   * bytes but delivering only 1, such that it passes length floor (>= 9),
   * MBAP length check (4 == 10 - 6), function check, and byte-count check
   * (2 == 2) but fails the final frame-size check (10 < 11). */
  f[0] = (uint8_t)(TID >> 8);
  f[1] = (uint8_t)(TID & 0xFFu);
  f[2] = 0u;
  f[3] = 0u;
  f[4] = 0u;
  f[5] = 0x04u; /* MBAP length = 4: unit + fc + byte_count + 1 data byte */
  f[6] = UNIT;
  f[7] = MODBUS_FC_READ_COILS;
  f[8] = 0x02u; /* byte count claims 2 bytes of data */
  f[9] = 0x15u; /* but only 1 byte is present (this is data[0]) */
  CHECK_EQ(modbus_parse_read_coils(f, 10u, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_TOO_SHORT);

  TESTS_REPORT("modbus_tcp");
}
