/**
 * @file    braille_disc.h
 * @brief   Maps a character to the disc angles that render it.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 * @version 0.1
 * @date    2026-07-20
 *
 * @copyright Copyright (c) 2026
 *
 * A braille cell is rendered by two discs: disc 1 has 4 weighted positions,
 * disc 2 has 16. Each character maps to a weight per disc, and the macros
 * below turn a weight into an absolute disc angle. Angles are expressed in
 * tenths of a degree so the modulo arithmetic stays integer.
 */

#ifndef BRAILLE_DISC_H
#define BRAILLE_DISC_H

#include <stdint.h>

/**
 * @defgroup braille_disc Braille Disc
 * @brief    Character-to-disc-angle translation.
 * @{
 */

/**
 * @name Disc position angles (tenths of a degree)
 *
 * Every value is multiplied by 10 so the angles are integers and work with
 * the modulo arithmetic in the weight-to-angle macros.
 * @{
 */
#define DISC_ANGLE_TO_POS_ANG_0 0     /**< Position 0: 0.0 deg.   */
#define DISC_ANGLE_TO_POS_ANG_1 225   /**< Position 1: 22.5 deg.  */
#define DISC_ANGLE_TO_POS_ANG_2 450   /**< Position 2: 45.0 deg.  */
#define DISC_ANGLE_TO_POS_ANG_3 675   /**< Position 3: 67.5 deg.  */
#define DISC_ANGLE_TO_POS_ANG_4 900   /**< Position 4: 90.0 deg.  */
#define DISC_ANGLE_TO_POS_ANG_5 1125  /**< Position 5: 112.5 deg. */
#define DISC_ANGLE_TO_POS_ANG_6 1350  /**< Position 6: 135.0 deg. */
#define DISC_ANGLE_TO_POS_ANG_7 1575  /**< Position 7: 157.5 deg. */
#define DISC_ANGLE_TO_POS_ANG_8 1800  /**< Position 8: 180.0 deg. */
#define DISC_ANGLE_TO_POS_ANG_9 2025  /**< Position 9: 202.5 deg. */
#define DISC_ANGLE_TO_POS_ANG_10 2250 /**< Position 10: 225.0 deg. */
#define DISC_ANGLE_TO_POS_ANG_11 2475 /**< Position 11: 247.5 deg. */
#define DISC_ANGLE_TO_POS_ANG_12 2700 /**< Position 12: 270.0 deg. */
#define DISC_ANGLE_TO_POS_ANG_13 2925 /**< Position 13: 292.5 deg. */
#define DISC_ANGLE_TO_POS_ANG_14 3150 /**< Position 14: 315.0 deg. */
#define DISC_ANGLE_TO_POS_ANG_15 3375 /**< Position 15: 337.5 deg. */
/** @} */

/**
 * @brief Absolute angle (0.1 deg) for a weight on disc 1 (4 positions).
 * @param WEIGHT Character weight for the single-row disc.
 * @return Angle in tenths of a degree, wrapped into one quarter turn.
 */
#define DISC1_ANGLE_TO_POS_ANG(WEIGHT)                                         \
  ((WEIGHT * DISC_ANGLE_TO_POS_ANG_1) % DISC_ANGLE_TO_POS_ANG_4)

/**
 * @brief Absolute angle (0.1 deg) for a weight on disc 2 (16 positions).
 * @param WEIGHT Character weight for the double-row disc.
 * @return Angle in tenths of a degree.
 */
#define DISC2_ANGLE_TO_POS_ANG(WEIGHT) (WEIGHT * DISC_ANGLE_TO_POS_ANG_1)

/**
 * @brief Render a character by turning both discs to its cell angles.
 *
 * Looks up each disc's weight for @p character, converts them to angles, and
 * drives the discs there.
 *
 * @param character Character to display.
 *
 * @note The per-disc weight/angle helpers are implementation details of
 *       braille_disc.c and stay file-static there.
 */
void translate_char_on_disc(uint8_t character);

/** @} */ // end of braille_disc

#endif // BRAILLE_DISC_H
