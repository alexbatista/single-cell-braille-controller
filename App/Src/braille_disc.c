// ----------------------------------------------------------------------------
// braille_disc.c
//
// Braille disc logic (portable, no STM32 headers). Not implemented yet;
// this placeholder keeps the build working since CMakeLists.txt already
// lists the file.
// ----------------------------------------------------------------------------

#include "braille_disc.h"

/**
 * @brief Get the character weight single row disc object.
 *
 * The result will be used in the argument of the function macros
 *
 * @param character
 * @return uint8_t
 * @note The return is the weight
 */
static uint8_t get_character_weight_single_row_disc(char character) {
  static const uint8_t weights[63] = {
  } // sequence of weights for each character in the alphabet
}

/**
 * @brief Get the character weight double row disc object
 *
 * The result will be used in the argument of the function macros
 *
 * @param character
 * @return uint8_t
 */
static uint8_t get_character_weight_double_row_disc(char character) {
}

static void move_single_row_disc() {
}
static void move_double_row_disc() {
}

static uint16_t character_to_position(uint8_t character) {
}

void Goto_character(uint8_t character) {

  move_single_row_disc();
  move_double_row_disc();
}