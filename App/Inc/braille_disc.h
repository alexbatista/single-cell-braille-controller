#ifndef BRAILLE_DISC_H
#define BRAILLE_DISC_H

// All elements are multiplied by 10 to become integer and work with mod
// function
#define DISC_ANGLE_TO_POS_ANG_0 0
#define DISC_ANGLE_TO_POS_ANG_1 225
#define DISC_ANGLE_TO_POS_ANG_2 450
#define DISC_ANGLE_TO_POS_ANG_3 675
#define DISC_ANGLE_TO_POS_ANG_4 900
#define DISC_ANGLE_TO_POS_ANG_5 1125
#define DISC_ANGLE_TO_POS_ANG_6 1350
#define DISC_ANGLE_TO_POS_ANG_7 1575
#define DISC_ANGLE_TO_POS_ANG_8 1800
#define DISC_ANGLE_TO_POS_ANG_9 2025
#define DISC_ANGLE_TO_POS_ANG_10 2250
#define DISC_ANGLE_TO_POS_ANG_11 2475
#define DISC_ANGLE_TO_POS_ANG_12 2700
#define DISC_ANGLE_TO_POS_ANG_13 2925
#define DISC_ANGLE_TO_POS_ANG_14 3150
#define DISC_ANGLE_TO_POS_ANG_15 3375

/**
 * @file braille_disc.h
 * @author Alex Batista (alexbatista.asb@gmail.com)
 * @brief Macro that defines the correspondent angle of a character on disc
 * @version 0.1
 * @date 2026-07-20
 *
 * @copyright Copyright (c) 2026
 *
 */
// Disc 1 has 4 weights, disc 2 as 16 weights
#define DISC1_ANGLE_TO_POS_ANG(WEIGHT)                                         \
  ((WEIGHT * DISC_ANGLE_TO_POS_ANG_1) % DISC_ANGLE_TO_POS_ANG_4)
#define DISC2_ANGLE_TO_POS_ANG(WEIGHT) (WEIGHT * DISC_ANGLE_TO_POS_ANG_1)

#include <stdint.h>

static uint8_t get_character_weight_single_row_disc(char c);
static uint8_t get_character_weight_double_row_disc(char c);
static void move_single_row_disc(void);
static void move_double_row_disc(void);
static uint16_t character_to_position(uint8_t character);

void Goto_character(uint8_t character);

#endif