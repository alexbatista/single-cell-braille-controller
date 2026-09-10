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
 * @name Value glyphs
 *
 * The two dot patterns the PLC reader uses for a boolean: no dots raised for
 * false, all six raised for true. They are deliberately not letters, so a
 * reader feeling a cell can always tell a value from a label glyph.
 * @{
 */
#define BRAILLE_DOTS_ALL_FLAT 0x00u   /**< No dot raised: blank cell. */
#define BRAILLE_DOTS_ALL_RAISED 0x3Fu /**< Dots 1-6 raised.           */
/** @} */

/**
 * @brief Render an explicit 6-dot pattern on the cell and block until the
 *        discs arrive.
 *
 * The dot bits follow the canonical Unicode Braille Patterns numbering:
 * bit 0 is dot 1, bit 1 dot 2, ... bit 5 dot 6. Bits above 5 are ignored.
 *
 * @param dots 6-dot bitmask, e.g. @ref BRAILLE_DOTS_ALL_RAISED.
 */
void braille_render_dots(uint8_t dots);

/**
 * @brief Render one Latin-1 character on the cell (case-insensitive).
 *
 * Use this for characters the firmware itself chooses, such as the packet's
 * label letters. Input arriving as a UTF-8 byte stream from a host must go
 * through @ref translate_char_on_disc instead.
 *
 * @param latin1_char Character code 0..255; unmapped codes render blank.
 */
void braille_render_char(uint8_t latin1_char);

/**
 * @brief Look up the 6-dot pattern for a Latin-1 character without moving.
 *
 * @param c Character code 0..255 (case-insensitive).
 * @return 6-dot bitmask; 0 for any unmapped code.
 */
uint8_t braille_pattern_for_char(uint8_t c);

/**
 * @brief Feed one byte of the incoming UTF-8 stream and render it when a whole
 *        character has arrived.
 *
 * Call this once per byte received from the host, in order. ASCII renders
 * immediately; a multi-byte character (the Portuguese accented letters arrive
 * as two bytes) is buffered until its last byte, then rendered as one move.
 * Bytes that complete nothing leave the discs where they are.
 *
 * @param byte Next byte received from the host, part of a UTF-8 stream.
 *
 * @note Characters above U+00FF have no cell on these discs and render blank.
 * @note Malformed input renders nothing at all and the discs stay put. That
 *       covers invalid encodings as well as truncated ones: overlong forms,
 *       surrogate halves and codepoints past U+10FFFF are dropped rather than
 *       aliased onto the character they would otherwise decode to.
 * @note The decoder keeps state between calls, so it is not reentrant: feed it
 *       from one context only (the main loop, not an ISR).
 * @note The per-disc weight/angle helpers are implementation details of
 *       braille_disc.c and stay file-static there.
 */
void translate_char_on_disc(uint8_t byte);

/** @} */ // end of braille_disc

#endif // BRAILLE_DISC_H
