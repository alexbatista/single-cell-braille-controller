// ----------------------------------------------------------------------------
// braille_disc.c
//
// Braille disc logic (portable, no STM32 headers). Not implemented yet;
// this placeholder keeps the build working since CMakeLists.txt already
// lists the file.
// ----------------------------------------------------------------------------

#include "braille_disc.h"
#include "motion_planner.h"
#include <stdint.h>

/**
 * @brief Get the character weight single row disc object.
 *
 * The result will be used in the argument of the function macros
 *
 * @param character
 * @return uint8_t
 * @note The return is the weight
 */
static uint16_t get_character_weight_single_row_disc(char character) {
  static const uint8_t weights[63] =
      {}; // sequence of weights for each character in the alphabet
}

/**
 * @brief Get the character weight double row disc object
 *
 * The result will be used in the argument of the function macros
 *
 * @param character
 * @return uint8_t
 */
static uint16_t get_character_weight_double_row_disc(char character) {
}

static uint16_t angle_single_row_disc(char character) {
  uint8_t weight = get_character_weight_single_row_disc(character);
  uint16_t angle = DISC1_ANGLE_TO_POS_ANG(weight);
  return angle;
}
static uint16_t angle_double_row_disc(char character) {
  uint8_t weight = get_character_weight_double_row_disc(character);
  uint16_t angle = DISC2_ANGLE_TO_POS_ANG(weight);
  return angle;
}

void translate_char_on_disc(uint8_t character) {

  disk_angles_t braille_cell = {0, 0};

  uint16_t angle_single_row = angle_single_row_disc(character);
  uint16_t angle_double_row = angle_double_row_disc(character);

  braille_cell.single_row_disc = angle_single_row;
  braille_cell.double_row_disc = angle_double_row;

  // move_to_angle(braille_cell);
}