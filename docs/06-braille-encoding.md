# 6. Braille encoding

This document describes how a character becomes a pair of disc angles: the
dot numbering, the positional binary mapping that assigns each dot
combination to an angle, the glyph table, and how the reader's value glyphs
relate to it. It also explains how to add a character. The motion that
follows is in [05-motion-and-homing.md](05-motion-and-homing.md).

**Source files:** `App/Src/braille_disc.c`, `App/Inc/braille_disc.h`,
`App/Inc/motion_planner.h` (`disk_angles_t`), `tests/test_braille_dots.c`.

## 6.1 Dots and discs

A braille cell has six dots, numbered 1–3 down the left column and 4–6 down the
right. The firmware stores a cell as a 6-bit mask with the same numbering as
the Unicode Braille Patterns block:

| Dot | Mask | | Dot | Mask |
|---|---|---|---|---|
| 1 | `DOT1` = 0x01 | | 4 | `DOT4` = 0x08 |
| 2 | `DOT2` = 0x02 | | 5 | `DOT5` = 0x10 |
| 3 | `DOT3` = 0x04 | | 6 | `DOT6` = 0x20 |

The cell is split between two embossed discs read side by side:

| Disc | Motor | Dots | Combinations | Groups on the rim |
|---|---|---|---|---|
| Disc 1 (single row) | 01 | 1, 4 (top row) | 4 | 16: the 4 combinations repeated 4 times |
| Disc 2 (double row) | 02 | 2, 3, 5, 6 | 16 | 16: each combination once |

Both discs have 16 groups at a 22.5° pitch.

## 6.2 The mapping

Each dot counts as one binary digit (raised = 1, flat = 0). Each disc's index
is the integer formed from its own dots, with the bit weight increasing with
the dot number:

```
n1 = d1 + 2·d4                    n1 ∈ 0..3      θ1 = 22.5° · n1
n2 = d2 + 2·d3 + 4·d5 + 8·d6      n2 ∈ 0..15     θ2 = 22.5° · n2
```

Angles are measured from the home position. Index 0 on both discs is the
all-flat group, i.e. the space character, and it is placed at the angle where
homing sets zero. After homing, the cell already shows a blank and no angular
offset is needed. The same mapping is published as Table I of the paper cited
in the root [README](../README.md#publication).

### Implementation

`braille_render_dots(uint8_t dots)`:

1. `get_character_weight_single_row_disc()` → `n1` (`DOT1` → 1, `DOT4` → 2).
2. `get_character_weight_double_row_disc()` → `n2` using `W2_DOT2` = 1,
   `W2_DOT3` = 2, `W2_DOT5` = 4, `W2_DOT6` = 8.
3. `DISC1_ANGLE_TO_POS_ANG(n1)` = `(n1 × 225) % 900` and
   `DISC2_ANGLE_TO_POS_ANG(n2)` = `n2 × 225`, in tenths of a degree. The
   modulo keeps disc 1 inside its first quarter turn; the three other copies of
   each combination are never used, so disc 1 never turns more than 67.5°
   between characters.
4. `move_to_angle()` with the two angles (blocks until both discs arrive).

The 16 angles are named `DISC_ANGLE_TO_POS_ANG_0` … `_15` in
`braille_disc.h` (0, 225, …, 3375).

## 6.3 Glyph table

`braille_pattern[256]` in `braille_disc.c` is the single source of truth for
characters. It is indexed by the ISO-8859-1 (Latin-1) code, which equals the
Unicode codepoint for U+0000–U+00FF. Patterns follow the Brazilian standard
*Grafia Braille para a Língua Portuguesa*.

| Letters | Patterns |
|---|---|
| A–J | 1 · 12 · 14 · 145 · 15 · 124 · 1245 · 125 · 24 · 245 |
| K–T | A–J plus dot 3 |
| U, V, X, Y, Z | 136 · 1236 · 1346 · 13456 · 1356 |
| W | 2456 |
| Á À Â Ã | 12356 · 1246 · 16 · 345 |
| Ç | 12346 |
| É Ê | 123456 · 126 |
| Í | 34 |
| Ó Ô Õ | 346 · 1456 · 246 |
| Ú Ü | 23456 · 1256 |

Every other code maps to 0, a blank cell. There are no digits or punctuation
in the table.

`braille_pattern_for_char(uint8_t c)` folds lowercase to uppercase before the
lookup: ASCII `a`–`z` by −0x20, and Latin-1 `0xE0`–`0xFE` by −0x20 except
`0xF7` (÷). `braille_render_char(uint8_t latin1_char)` is
`braille_render_dots(braille_pattern_for_char(c))`.

## 6.4 Value glyphs and the label invariant

The reader ([09-reader-ui.md](09-reader-ui.md)) shows a signal's value as a raw
pattern, not a character:

| Constant | Pattern | Meaning |
|---|---|---|
| `BRAILLE_DOTS_ALL_FLAT` | 0x00 | `false` |
| `BRAILLE_DOTS_ALL_RAISED` | 0x3F | `true` |

A label is one or two letters followed by the value. The reader can tell where
the label ends only because **no label letter uses either value pattern**.
All-flat is the space, which no label contains. All-raised is **É** (dots
1–6); no label may contain É. `plc_packet.c` holds the label table;
`tests/test_plc_packet.c` checks it.

## 6.5 Input decoding (USB_CDC variant)

`translate_char_on_disc(uint8_t byte)` accepts a UTF-8 byte stream, rebuilds
the codepoint, rejects malformed, overlong, surrogate and out-of-range
sequences, and renders the low byte through the Latin-1 table (codepoints above
U+00FF render blank). Only the USB_CDC variant calls it; the MODBUS reader calls
`braille_render_char()` and `braille_render_dots()` directly. The decoder is
described in [14-usb-cdc-variant.md](14-usb-cdc-variant.md#145-utf-8-decoder).

## 6.6 Adding or changing a character

1. Add or edit the row in `braille_pattern[]`, indexed by the **uppercase**
   Latin-1 code (`['X']` for ASCII, `[0xC9]`-style hex for accented letters so
   the source stays plain ASCII). Write the pattern as an OR of `DOTn`.
2. A lowercase form needs nothing more if its Latin-1 code is exactly 0x20
   above the uppercase one; otherwise extend `braille_pattern_for_char()`.
3. If the new character is used as a label letter, it must not be all-flat or
   all-raised (section 6.4).
4. Add a check to `tests/test_braille_dots.c`: the expected mask from
   `braille_pattern_for_char()`, and if useful the disc angles recorded by the
   suite's fake `move_to_angle()` (`expect_move(single, double)`).
5. Run `make -C tests`.

The table costs 256 B of flash and no RAM, whatever its contents.
