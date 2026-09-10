/* Task 4 -- the packet contract. Most of this suite guards invariants rather
 * than behaviour, because the table is data an editor will come back to: the
 * coil range must match what the MODBUS request asks for, and no label glyph
 * may collide with a value glyph. */
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>

#include "motion_planner.h"

/* braille_disc.c is included for braille_pattern_for_char(); it needs
 * move_to_angle(), which no check here triggers. */
void move_to_angle(disk_angles_t disk_angle) { (void)disk_angle; }
void initialize_motors(UART_HandleTypeDef *a, UART_HandleTypeDef *b,
                       TIM_HandleTypeDef *c, TIM_HandleTypeDef *d) {
  (void)a; (void)b; (void)c; (void)d;
}
bool calibrate_zero_position(void) { return true; }

#include "braille_disc.c"
#include "plc_packet.c"

int main(void) {
  /* The table and the MODBUS request must agree about the package size. */
  CHECK_EQ(PLC_FIELD_COUNT, PLC_COIL_COUNT);
  CHECK_EQ(PLC_FIELD_COUNT, 11u);

  /* Out-of-range lookups are rejected, not clamped. */
  CHECK(plc_packet_field(0u) != NULL);
  CHECK(plc_packet_field((uint8_t)(PLC_FIELD_COUNT - 1u)) != NULL);
  CHECK(plc_packet_field((uint8_t)PLC_FIELD_COUNT) == NULL);

  /* Coils are unique, ascending, and inside the range we request. */
  for (uint8_t i = 0u; i < PLC_FIELD_COUNT; ++i) {
    const plc_field_t *f = plc_packet_field(i);
    CHECK(f->coil >= PLC_COIL_BASE_ADDRESS);
    CHECK(f->coil < PLC_COIL_BASE_ADDRESS + PLC_COIL_COUNT);
    if (i > 0u) {
      CHECK(f->coil > plc_packet_field((uint8_t)(i - 1u))->coil);
    }
  }

  /* Labels are 1..2 glyphs and match the design's table. */
  const char *expected[PLC_FIELD_COUNT] = {"AE", "AS", "BE", "BS", "P",
                                           "S",  "T",  "V",  "R",  "M", "L"};
  for (uint8_t i = 0u; i < PLC_FIELD_COUNT; ++i) {
    const plc_field_t *f = plc_packet_field(i);
    CHECK(f->glyph_count >= 1u);
    CHECK(f->glyph_count <= PLC_LABEL_MAX_GLYPHS);
    char got[PLC_LABEL_MAX_GLYPHS + 1u];
    for (uint8_t g = 0u; g < f->glyph_count; ++g) {
      got[g] = (char)f->label[g];
    }
    got[f->glyph_count] = '\0';
    CHECK_STR_EQ(got, expected[i]);
  }

  /* THE invariant that makes variable-length labels readable: every label
   * glyph is a real letter, and none of them is all-flat or all-raised, so a
   * value glyph can never be mistaken for a label glyph still to come.
   * Adding a label letter such as E-acute (all six dots) would break this. */
  for (uint8_t i = 0u; i < PLC_FIELD_COUNT; ++i) {
    const plc_field_t *f = plc_packet_field(i);
    for (uint8_t g = 0u; g < f->glyph_count; ++g) {
      uint8_t dots = braille_pattern_for_char(f->label[g]);
      CHECK(dots != BRAILLE_DOTS_ALL_FLAT);
      CHECK(dots != BRAILLE_DOTS_ALL_RAISED);
    }
  }

  /* Snapshot accessors: bit position follows the field's coil offset, so
   * display order and coil order are independent. */
  plc_snapshot_t none = {0x0000u};
  plc_snapshot_t all = {0x07FFu}; /* 11 bits set */
  plc_snapshot_t first_only = {0x0001u};
  plc_snapshot_t last_only = {0x0400u}; /* bit 10 */

  for (uint8_t i = 0u; i < PLC_FIELD_COUNT; ++i) {
    CHECK(plc_snapshot_bit(none, i) == false);
    CHECK(plc_snapshot_bit(all, i) == true);
  }
  CHECK(plc_snapshot_bit(first_only, 0u) == true);
  CHECK(plc_snapshot_bit(first_only, 1u) == false);
  CHECK(plc_snapshot_bit(last_only, 10u) == true);
  CHECK(plc_snapshot_bit(last_only, 9u) == false);

  CHECK(plc_snapshot_equal(none, none) == true);
  CHECK(plc_snapshot_equal(none, all) == false);
  CHECK(plc_snapshot_equal(all, all) == true);

  TESTS_REPORT("plc_packet");
}
