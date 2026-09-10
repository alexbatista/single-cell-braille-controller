// ----------------------------------------------------------------------------
// plc_packet.c
//
// The package contract. The table below is the single place that knows what
// the PLC's coils mean; everything else works in field indices.
//
// Label letters are Portuguese mnemonics: E is "entrada" and S is "saida" for
// the SMEMA handshake directions, R is "veRmelho" and M is "aMarelo" so the
// three signal-lamp colours stay distinct, and L is "liberado" for released.
// ----------------------------------------------------------------------------

#include "plc_packet.h"

#include "plc_config.h"

_Static_assert(PLC_FIELD_COUNT == PLC_COIL_COUNT,
               "the label table and the MODBUS request disagree about the "
               "package size");
_Static_assert(PLC_COIL_COUNT <= 16u,
               "plc_snapshot_t holds the package in a uint16_t");
_Static_assert(PLC_LABEL_MAX_GLYPHS == 2u,
               "the table below writes at most two label glyphs per row");

// Reading order, which is also coil order here. Cost: 44 B flash, 0 B RAM.
static const plc_field_t plc_fields[PLC_FIELD_COUNT] = {
    {0u, 2u, {'A', 'E'}},   // SMEMA A IN
    {1u, 2u, {'A', 'S'}},   // SMEMA A OUT
    {2u, 2u, {'B', 'E'}},   // SMEMA B IN
    {3u, 2u, {'B', 'S'}},   // SMEMA B OUT
    {4u, 1u, {'P', 0u}},    // SENSOR PRESENCA
    {5u, 1u, {'S', 0u}},    // STOP_LINE
    {6u, 1u, {'T', 0u}},    // SEND_TIME
    {7u, 1u, {'V', 0u}},    // VERDE
    {8u, 1u, {'R', 0u}},    // VERMELHO
    {9u, 1u, {'M', 0u}},    // AMARELO
    {10u, 1u, {'L', 0u}},   // released
};

const plc_field_t *plc_packet_field(uint8_t index) {
  if (index >= PLC_FIELD_COUNT) {
    return NULL;
  }
  return &plc_fields[index];
}

bool plc_snapshot_bit(plc_snapshot_t snapshot, uint8_t index) {
  const plc_field_t *field = plc_packet_field(index);
  if (field == NULL) {
    return false;
  }
  uint8_t bit = (uint8_t)(field->coil - PLC_COIL_BASE_ADDRESS);
  return ((snapshot.bits >> bit) & 1u) != 0u;
}

bool plc_snapshot_equal(plc_snapshot_t a, plc_snapshot_t b) {
  return a.bits == b.bits;
}
