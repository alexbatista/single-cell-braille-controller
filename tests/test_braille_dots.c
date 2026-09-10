/* Task 1 -- the dot-bitmask primitive, plus a regression guard that the
 * existing character path still produces the angles it did before the
 * refactor. The all-flat/all-raised pair is what the reader uses for a
 * false/true value, so those two angles are the load-bearing cases. */
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>

/* move_to_angle() lives in motion_planner.c, which needs the real HAL. The
 * test supplies it instead and records what the module asked for. */
#include "motion_planner.h"

static disk_angles_t last_move;
static int move_count;

void move_to_angle(disk_angles_t disk_angle) {
  last_move = disk_angle;
  ++move_count;
}
void initialize_motors(UART_HandleTypeDef *a, UART_HandleTypeDef *b,
                       TIM_HandleTypeDef *c, TIM_HandleTypeDef *d) {
  (void)a; (void)b; (void)c; (void)d;
}
bool calibrate_zero_position(void) { return true; }

#include "braille_disc.c"

static void expect_move(uint16_t single, uint16_t doubl) {
  CHECK_EQ(last_move.single_row_disc, single);
  CHECK_EQ(last_move.double_row_disc, doubl);
}

int main(void) {
  /* All flat is the FALSE glyph: both discs at zero. */
  braille_render_dots(BRAILLE_DOTS_ALL_FLAT);
  expect_move(0u, 0u);

  /* All raised is the TRUE glyph. disc1 weight 3 -> (3*225)%900 = 675;
   * disc2 weight 15 -> 15*225 = 3375. */
  braille_render_dots(BRAILLE_DOTS_ALL_RAISED);
  expect_move(675u, 3375u);

  /* Every label letter used by the packet contract (spec section 5). */
  braille_render_char('A'); expect_move(225u, 0u);
  braille_render_char('B'); expect_move(225u, 225u);
  braille_render_char('E'); expect_move(225u, 900u);
  braille_render_char('S'); expect_move(450u, 675u);
  braille_render_char('P'); expect_move(675u, 675u);
  braille_render_char('T'); expect_move(450u, 1575u);
  braille_render_char('V'); expect_move(225u, 2475u);
  braille_render_char('R'); expect_move(225u, 1575u);
  braille_render_char('M'); expect_move(675u, 450u);
  braille_render_char('L'); expect_move(225u, 675u);

  /* Lowercase folds to uppercase, as before the refactor. */
  braille_render_char('a'); expect_move(225u, 0u);

  /* The pattern query the packet test relies on. */
  CHECK_EQ(braille_pattern_for_char('A'), 0x01u);
  CHECK_EQ(braille_pattern_for_char('L'), 0x07u);
  CHECK_EQ(braille_pattern_for_char('\0'), 0x00u);

  /* Regression: the UTF-8 stream entry point still behaves as documented.
   * 'a' renders immediately; a lone lead byte renders nothing. */
  move_count = 0;
  translate_char_on_disc('a');
  CHECK_EQ(move_count, 1);
  expect_move(225u, 0u);
  move_count = 0;
  translate_char_on_disc(0xC3u); /* lead byte of a 2-byte sequence */
  CHECK_EQ(move_count, 0);
  translate_char_on_disc(0xA7u); /* c-cedilla completes: dots 1,2,3,4,6 */
  CHECK_EQ(move_count, 1);

  TESTS_REPORT("braille_dots");
}
