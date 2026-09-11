// ----------------------------------------------------------------------------
// braille_disc.c
//
// Character-to-disc-angle translation for a single braille cell rendered by
// two rotating discs. The cell's 6 dots are stored once per character as a
// bitmask (the single source of truth), reachable through
// braille_render_dots(); each disc's "weight" is derived from that mask and
// handed to the DISCx_ANGLE_TO_POS_ANG macros in braille_disc.h.
//
// Input arrives one byte at a time as a UTF-8 stream, so bytes are decoded into
// a character code before any lookup happens.
// ----------------------------------------------------------------------------

#include "braille_disc.h"
#include "motion_planner.h"
#include <stdint.h>

// ---- Canonical 6-dot bit numbering (matches Unicode Braille Patterns) ------
//   1 4      DOT1 DOT4
//   2 5  ->  DOT2 DOT5
//   3 6      DOT3 DOT6
#define DOT1 0x01u
#define DOT2 0x02u
#define DOT3 0x04u
#define DOT4 0x08u
#define DOT5 0x10u
#define DOT6 0x20u

/**
 * @brief Braille dot pattern for each character (single source of truth).
 *
 * One 6-dot bitmask per character; each row is directly verifiable against a
 * braille chart. Indexed by the character code in ISO-8859-1 (Latin-1), which
 * for U+0000..U+00FF is the Unicode codepoint itself. A-Z use ASCII
 * char-literal indices; the Portuguese accented letters use explicit Latin-1
 * hex indices (not accented char literals) so this file stays UTF-8-clean and
 * needs no -fexec-charset. Any code not listed stays 0 => blank cell.
 * Cost: ~256 B flash, 0 B RAM.
 *
 * Patterns follow the Brazilian standard "Grafia Braille para a Lingua
 * Portuguesa" (IBC/MEC).
 */
static const uint8_t braille_pattern[256] = {
    ['A'] = DOT1,
    ['B'] = DOT1 | DOT2,
    ['C'] = DOT1 | DOT4,
    ['D'] = DOT1 | DOT4 | DOT5,
    ['E'] = DOT1 | DOT5,
    ['F'] = DOT1 | DOT2 | DOT4,
    ['G'] = DOT1 | DOT2 | DOT4 | DOT5,
    ['H'] = DOT1 | DOT2 | DOT5,
    ['I'] = DOT2 | DOT4,
    ['J'] = DOT2 | DOT4 | DOT5,
    ['K'] = DOT1 | DOT3,
    ['L'] = DOT1 | DOT2 | DOT3,
    ['M'] = DOT1 | DOT3 | DOT4,
    ['N'] = DOT1 | DOT3 | DOT4 | DOT5,
    ['O'] = DOT1 | DOT3 | DOT5,
    ['P'] = DOT1 | DOT2 | DOT3 | DOT4,
    ['Q'] = DOT1 | DOT2 | DOT3 | DOT4 | DOT5,
    ['R'] = DOT1 | DOT2 | DOT3 | DOT5,
    ['S'] = DOT2 | DOT3 | DOT4,
    ['T'] = DOT2 | DOT3 | DOT4 | DOT5,
    ['U'] = DOT1 | DOT3 | DOT6,
    ['V'] = DOT1 | DOT2 | DOT3 | DOT6,
    ['W'] = DOT2 | DOT4 | DOT5 | DOT6,
    ['X'] = DOT1 | DOT3 | DOT4 | DOT6,
    ['Y'] = DOT1 | DOT3 | DOT4 | DOT5 | DOT6,
    ['Z'] = DOT1 | DOT3 | DOT5 | DOT6,
    // Portuguese accented letters (index = ISO-8859-1 uppercase byte).
    [0xC1] = DOT1 | DOT2 | DOT3 | DOT5 | DOT6,        // A-acute   (A)
    [0xC0] = DOT1 | DOT2 | DOT4 | DOT6,               // A-grave   (A)
    [0xC2] = DOT1 | DOT6,                             // A-circ    (A)
    [0xC3] = DOT3 | DOT4 | DOT5,                      // A-tilde   (A)
    [0xC7] = DOT1 | DOT2 | DOT3 | DOT4 | DOT6,        // C-cedilla (C)
    [0xC9] = DOT1 | DOT2 | DOT3 | DOT4 | DOT5 | DOT6, // E-acute   (E)
    [0xCA] = DOT1 | DOT2 | DOT6,                      // E-circ    (E)
    [0xCD] = DOT3 | DOT4,                             // I-acute   (I)
    [0xD3] = DOT3 | DOT4 | DOT6,                      // O-acute   (O)
    [0xD4] = DOT1 | DOT4 | DOT5 | DOT6,               // O-circ    (O)
    [0xD5] = DOT2 | DOT4 | DOT6,                      // O-tilde   (O)
    [0xDA] = DOT2 | DOT3 | DOT4 | DOT5 | DOT6,        // U-acute   (U)
    [0xDC] = DOT1 | DOT2 | DOT5 | DOT6,               // U-diaeresis (U)
};

/**
 * @brief Map a Latin-1 character code to its braille dot pattern
 *        (case-insensitive).
 *
 * The single seam where the character encoding lives: lowercase (ASCII and
 * Latin-1 accented) is folded to uppercase, then looked up.
 */
uint8_t braille_pattern_for_char(uint8_t c) {
  if (c >= 'a' && c <= 'z') {
    c -= 0x20; // ASCII lower -> upper
  } else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) {
    c -= 0x20; // Latin-1 accented lower -> upper (0xF7 is division sign, skip)
  }
  return braille_pattern[c];
}

// ---- Input encoding: UTF-8 stream -> character code -------------------------
// The host terminal speaks UTF-8, so an accented letter arrives as more than
// one byte (c-cedilla is 0xC3 0xA7). The bytes cannot be rendered one at a
// time: 0xC3 alone is A-tilde in Latin-1 and the continuation byte is unmapped,
// so a per-byte renderer would drive the cell to that letter and then straight
// back to blank for every accented key. The sequence has to be reassembled
// before anything is looked up.
//
// Decoding is cheap because the pattern table is indexed by Latin-1, which for
// U+0000..U+00FF *is* the codepoint: rebuild the codepoint, keep its low byte.
// Codepoints above U+00FF have no cell on these discs and render blank.

/** @brief Returned while a sequence is still incomplete (or was malformed). */
#define UTF8_INCOMPLETE 0xFFFFFFFFu

/** @brief Highest codepoint Unicode defines; longer sequences encode nothing. */
#define UTF8_MAX_CODEPOINT 0x10FFFFu

/**
 * @name UTF-16 surrogate half range, which UTF-8 must never encode.
 * @{
 */
#define UTF8_SURROGATE_FIRST 0xD800u
#define UTF8_SURROGATE_LAST 0xDFFFu
/** @} */

static uint32_t utf8_codepoint;      /**< bits gathered from the sequence   */
static uint32_t utf8_min_codepoint;  /**< smallest value this length may hold */
static uint8_t utf8_bytes_remaining; /**< continuation bytes still expected */

/**
 * @brief Feed one byte of a UTF-8 stream to the decoder.
 *
 * Malformed input never leaves the decoder stuck: a lead byte followed by
 * anything other than a continuation byte restarts decoding from that byte.
 *
 * A completed sequence is only reported once it is a *valid* encoding. UTF-8
 * gives every codepoint exactly one spelling, so three families of sequence
 * are rejected even though their bits decode cleanly:
 *  - overlong forms, which spell a short codepoint in a longer sequence
 *    (0xC1 0x81 would otherwise alias to 'A' and skip the ASCII fast path);
 *  - the UTF-16 surrogate halves U+D800..U+DFFF, which are not characters;
 *  - anything past U+10FFFF, which Unicode does not define.
 * Each is dropped like any other malformed input: nothing renders, and the
 * next lead byte starts a fresh sequence.
 *
 * @param byte Next byte received from the host.
 * @return The decoded codepoint, or @ref UTF8_INCOMPLETE while the current
 *         sequence still needs more bytes, or when it turned out invalid
 *         (nothing to render either way).
 */
static uint32_t utf8_decode_byte(uint8_t byte) {
  if (byte < 0x80u) { // ASCII: a whole character on its own
    utf8_bytes_remaining = 0u;
    return byte;
  }

  if ((byte & 0xC0u) == 0x80u) { // continuation byte: 10xxxxxx
    if (utf8_bytes_remaining == 0u) {
      return UTF8_INCOMPLETE; // stray byte with no lead: drop it
    }
    utf8_codepoint = (utf8_codepoint << 6) | (byte & 0x3Fu);
    if (--utf8_bytes_remaining != 0u) {
      return UTF8_INCOMPLETE;
    }
    if (utf8_codepoint < utf8_min_codepoint || // overlong for this length
        (utf8_codepoint >= UTF8_SURROGATE_FIRST &&
         utf8_codepoint <= UTF8_SURROGATE_LAST) ||
        utf8_codepoint > UTF8_MAX_CODEPOINT) {
      return UTF8_INCOMPLETE; // decodes cleanly but is not a valid encoding
    }
    return utf8_codepoint;
  }

  // Lead byte: its high bits say how many continuation bytes follow, and the
  // length fixes the smallest codepoint the sequence is allowed to carry.
  if ((byte & 0xE0u) == 0xC0u) { // 110xxxxx
    utf8_codepoint = byte & 0x1Fu;
    utf8_min_codepoint = 0x80u;
    utf8_bytes_remaining = 1u;
  } else if ((byte & 0xF0u) == 0xE0u) { // 1110xxxx
    utf8_codepoint = byte & 0x0Fu;
    utf8_min_codepoint = 0x800u;
    utf8_bytes_remaining = 2u;
  } else if ((byte & 0xF8u) == 0xF0u) { // 11110xxx
    utf8_codepoint = byte & 0x07u;
    utf8_min_codepoint = 0x10000u;
    utf8_bytes_remaining = 3u;
  } else {
    utf8_bytes_remaining = 0u; // 0xF8..0xFF are never valid UTF-8
  }
  return UTF8_INCOMPLETE;
}

// ---- Disc 2 geometry -------------------------------------------------------
// Weight is the 4-bit number [d6 d5 d3 d2] with d6 as the most significant
// bit, matching the physical layout of the double-row disc's 16 positions.
#define W2_DOT2 0x01u // LSB
#define W2_DOT3 0x02u
#define W2_DOT5 0x04u
#define W2_DOT6 0x08u // MSB

/**
 * @brief Get the weight for the single-row disc (top row: dots 1,4).
 * @param pattern 6-dot bitmask.
 * @return Weight in 0..3 (dot 1 -> 1, dot 4 -> 2).
 */
static uint8_t get_character_weight_single_row_disc(uint8_t pattern) {
  uint8_t weight = 0u;
  if (pattern & DOT1) {
    weight |= 1u; /* dot 1 -> value 1 */
  }
  if (pattern & DOT4) {
    weight |= 2u; /* dot 4 -> value 2 */
  }
  return weight;
}

/**
 * @brief Get the weight for the double-row disc (dots 2,3,5,6).
 * @param pattern 6-dot bitmask.
 * @return Weight in 0..15 ([d6 d5 d3 d2], d6 most significant).
 */
static uint8_t get_character_weight_double_row_disc(uint8_t pattern) {
  uint8_t weight = 0u;
  if (pattern & DOT2) {
    weight |= W2_DOT2;
  }
  if (pattern & DOT3) {
    weight |= W2_DOT3;
  }
  if (pattern & DOT5) {
    weight |= W2_DOT5;
  }
  if (pattern & DOT6) {
    weight |= W2_DOT6;
  }
  return weight;
}

void braille_render_dots(uint8_t dots) {
  disk_angles_t braille_cell = {0, 0};

  uint8_t weight_single = get_character_weight_single_row_disc(dots);
  uint8_t weight_double = get_character_weight_double_row_disc(dots);

  braille_cell.single_row_disc = DISC1_ANGLE_TO_POS_ANG(weight_single);
  braille_cell.double_row_disc = DISC2_ANGLE_TO_POS_ANG(weight_double);

  move_to_angle(braille_cell);
}

void braille_render_char(uint8_t latin1_char) {
  braille_render_dots(braille_pattern_for_char(latin1_char));
}

void translate_char_on_disc(uint8_t byte) {
  uint32_t codepoint = utf8_decode_byte(byte);
  if (codepoint == UTF8_INCOMPLETE) {
    return; // mid-sequence: no character to render yet, discs stay put
  }

  // Outside Latin-1 there is no cell for the character, so render a blank one
  // rather than aliasing it onto some other letter.
  braille_render_char((codepoint <= 0xFFu) ? (uint8_t)codepoint : 0u);
}
