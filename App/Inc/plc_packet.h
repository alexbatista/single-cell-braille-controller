/**
 * @file    plc_packet.h
 * @brief   The fixed package of PLC signals and how it is labelled.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * The PLC always serves the same package: a run of coils whose meaning is
 * fixed at build time. This module is that contract -- which coil carries
 * which signal, and the braille letters that name it -- plus the snapshot
 * type the rest of the firmware passes around.
 *
 * Labels are one or two letters. That is safe only because a value renders as
 * all-flat or all-raised and no label letter is either, so the value always
 * announces itself as the end of the sequence. Never add a label glyph whose
 * pattern is @ref BRAILLE_DOTS_ALL_FLAT or @ref BRAILLE_DOTS_ALL_RAISED.
 */

#ifndef PLC_PACKET_H
#define PLC_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "plc_config.h"

/**
 * @defgroup plc_packet PLC Packet
 * @brief    The fixed signal package and its braille labels.
 * @{
 */

/** @brief Fields in one package; static-asserted equal to PLC_COIL_COUNT. */
#define PLC_FIELD_COUNT 11u

/** @brief Longest label, in glyphs. Each glyph costs one disc move. */
#define PLC_LABEL_MAX_GLYPHS 2u

/**
 * @brief One package's worth of coil states.
 *
 * Wrapped in a struct so it cannot be confused with a plain integer and so
 * the accessors are the only way in. One bit per coil, bit @c n being the
 * coil at @c PLC_COIL_BASE_ADDRESS+n.
 */
typedef struct {
  uint16_t bits; /**< Coil states, LSB = first coil of the package. */
} plc_snapshot_t;

/** @brief One monitored signal: where to read it and what to call it. */
typedef struct {
  uint8_t coil;        /**< Absolute coil address.                        */
  uint8_t glyph_count; /**< Label length, 1..PLC_LABEL_MAX_GLYPHS.        */
  uint8_t label[PLC_LABEL_MAX_GLYPHS]; /**< Label letters, ASCII.         */
} plc_field_t;

/**
 * @brief Look up one field of the package.
 *
 * @param index Field index in reading order, 0..PLC_FIELD_COUNT-1.
 * @return The field, or NULL when @p index is out of range.
 */
const plc_field_t *plc_packet_field(uint8_t index);

/**
 * @brief Read one field's boolean state out of a snapshot.
 *
 * The bit is selected by the field's coil address, not by @p index, so the
 * order fields are read aloud in is independent of their coil order.
 *
 * @param snapshot Snapshot to read.
 * @param index    Field index, 0..PLC_FIELD_COUNT-1.
 * @return The signal's state; false for an out-of-range index.
 */
bool plc_snapshot_bit(plc_snapshot_t snapshot, uint8_t index);

/**
 * @brief Compare two snapshots.
 *
 * This is how a background poll decides whether anything actually changed,
 * so it must stay a whole-package comparison.
 *
 * @param a First snapshot.
 * @param b Second snapshot.
 * @return true when every coil matches.
 */
bool plc_snapshot_equal(plc_snapshot_t a, plc_snapshot_t b);

/** @} */ // end of plc_packet

#endif // PLC_PACKET_H
