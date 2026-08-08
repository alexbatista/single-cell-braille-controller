# 07 — Character encoding: from a keypress to a disc angle

This guide explains the UTF-8 decoder in `App/Src/braille_disc.c` — the piece
that sits between "a byte arrived over USB" and "look this character up in the
braille table."

It exists because of a real bug: **every accented letter made both discs move
and then immediately move back.** That symptom is the best possible teacher for
this topic, so the guide is built around it.

Every hex number in the code is explained here in terms of *what it means about
a character*, not as a magic constant. If you only remember one thing:

> Hex numbers in this decoder are never "the letter X". They are either
> **markers** (bit patterns that say "I am the first byte" / "I am a leftover
> byte") or **masks** (which say "ignore the marker bits, keep the payload").

---

## 1. Three different things that all get called "a character"

The whole bug comes from confusing these three. Keep them separate:

| Thing | Example for `ç` | Who owns it |
|---|---|---|
| **The key you press** | the `ç` key | you, at the keyboard |
| **The codepoint** — the character's official ID number in Unicode | `U+00E7`, which is just decimal **231** | the Unicode standard |
| **The bytes on the wire** — how that ID gets packed for transmission | `0xC3` then `0xA7` — **two** bytes | the *encoding* (UTF-8) |

A codepoint is a number. A byte stream is how you ship that number. **An
encoding is the packing rule that converts between them.** UTF-8 is one such
rule; Latin-1 (ISO-8859-1) is another.

The critical asymmetry:

- **ASCII letters** (`a`–`z`, `A`–`Z`): codepoint fits in one byte, and UTF-8
  ships it as exactly one byte. One keypress → one byte.
- **Accented letters**: codepoint `231` still fits in one byte numerically, but
  **UTF-8 refuses to ship any codepoint ≥ 128 as a single byte** (section 4
  explains why). One keypress → *two* bytes.

That asymmetry — one keypress, two bytes — is the entire bug. The old code
rendered a braille cell for *every byte*, so it rendered two cells for one
letter.

---

## 2. What the braille table is actually indexed by

Look at the table in [`braille_disc.c:42`](../App/Src/braille_disc.c#L42):

```c
static const uint8_t braille_pattern[256] = {
    ['A'] = DOT1,
    ...
    [0xC7] = DOT1 | DOT2 | DOT3 | DOT4 | DOT6,        // C-cedilla
```

256 slots, so the index must be a number in 0–255. The encoding it assumes is
**Latin-1**, whose defining property is beautifully simple:

> In Latin-1, the byte value **is** the codepoint, for codepoints U+0000
> through U+00FF.

So `0xC7` = 199 = `U+00C7` = `Ç`. The row `[0xC7]` genuinely is "the Ç row".
Here, `0xC7` really does mean a letter. This is the **only** place in the flow
where a hex number names a character — and it's the reason the fix needed zero
changes to the table.

`pattern_for_char()` at [`braille_disc.c:95`](../App/Src/braille_disc.c#L95)
takes such a number and folds lowercase to uppercase before the lookup.

**So the decoder's one job is:** turn a stream of UTF-8 bytes back into these
0–255 character codes.

---

## 3. The bug, told as a story

Before the fix, `App_run()` popped one byte from the USB ring buffer and passed
it straight to the table as if it were a Latin-1 code. You press `ç` once, two
bytes arrive, and the firmware renders **two characters**:

**Byte 1 — `0xC3`.** Interpreted as Latin-1, 0xC3 = 195 = `U+00C3` = **`Ã`**.
That row *exists* in the table (A-tilde is a Portuguese letter!). So the discs
dutifully drove to Ã's cell: disc 1 → 45.0°, disc 2 → 135.0°.

**Byte 2 — `0xA7`.** Interpreted as Latin-1, 0xA7 = `§`, the section sign. Not
a braille letter, so its row is `0` — a **blank cell**. The discs drove back to
0.0°/0.0°.

Move out, move back. Exactly what you saw.

Now the two details that make this a *systematic* failure rather than bad luck:

**(a) Every Portuguese accented letter has the same first byte, `0xC3`.**
That's why they all behaved identically:

| you press | codepoint | bytes sent | byte 1 read as Latin-1 | byte 2 read as Latin-1 |
|---|---|---|---|---|
| `á` | U+00E1 | `C3 A1` | `Ã` → moves | `¡` → blank |
| `ã` | U+00E3 | `C3 A3` | `Ã` → moves | `£` → blank |
| `ç` | U+00E7 | `C3 A7` | `Ã` → moves | `§` → blank |
| `é` | U+00E9 | `C3 A9` | `Ã` → moves | `©` → blank |
| `ô` | U+00F4 | `C3 B4` | `Ã` → moves | `´` → blank |
| `ú` | U+00FA | `C3 BA` | `Ã` → moves | `º` → blank |
| `Ç` | U+00C7 | `C3 87` | `Ã` → moves | (unassigned) → blank |

Section 4 shows *why* `0xC3` is shared by all of them — it's arithmetic, not
coincidence.

**(b) The second byte can never accidentally work.** UTF-8 continuation bytes
are always in the range `0x80`–`0xBF`, and the table populates only `A`–`Z`
plus `0xC0`–`0xDC`. The two ranges never overlap, so the second byte was
*guaranteed* to render blank. The bug had no lucky cases.

---

## 4. How UTF-8 packs a codepoint into bytes

UTF-8's design goal: stay byte-compatible with ASCII, but still be able to ship
any of Unicode's ~1.1 million codepoints. Its trick is to spend the top bits of
each byte on **markers** and the remaining bits on **payload**.

Here are the four byte shapes. Read `x` as "payload bit — part of the actual
codepoint" and the leading `0`/`1`s as the marker:

```
1 byte :  0xxxxxxx                                7 payload bits
2 bytes:  110xxxxx  10xxxxxx                     11 payload bits
3 bytes:  1110xxxx  10xxxxxx  10xxxxxx           16 payload bits
4 bytes:  11110xxx  10xxxxxx  10xxxxxx  10xxxxxx 21 payload bits
```

Three rules fall straight out of that picture, and they are the whole spec you
need:

1. **A byte with a leading `0` is a complete ASCII character.** Values 0–127.
   This is why UTF-8 is a superset of ASCII.
2. **A byte starting `110`, `1110`, or `11110` is a *lead byte*.** The count of
   `1`s before the first `0` tells you the total length of the sequence: `110` →
   2 bytes, `1110` → 3 bytes, `11110` → 4 bytes.
3. **A byte starting `10` is a *continuation byte*** — a middle-or-end piece,
   meaningless on its own. This is the self-synchronising property: you can
   always tell a lead byte from a continuation byte, so a decoder can never get
   permanently lost.

Note the consequence: **every codepoint ≥ 128 needs a lead byte**, and lead
bytes all start with `11`, i.e. `0xC0` or higher. So `ç` cannot be shipped as
one byte even though 231 fits in eight bits. Byte 231 (`1110 0111`) starts with
`1110`, which UTF-8 has already reserved to mean "a 3-byte sequence starts
here." The meaning is taken.

### Packing `ç` by hand

`ç` = U+00E7 = 231. It needs 2 bytes, which carry 11 payload bits, so write 231
as 11 bits:

```
231 in 11 bits:    00011 100111
                   └─┬─┘ └──┬─┘
        top 5 bits = 3      low 6 bits = 39
```

Drop those two groups into the `110xxxxx 10xxxxxx` template:

```
lead byte:  110 00011  = 1100 0011 = 0xC3
cont byte:  10  100111 = 1010 0111 = 0xA7
```

There it is — `C3 A7`, the bytes your terminal actually sends.

### Why *all* accented letters share `0xC3`

Look at what the lead byte carries: only the **top 5 of the 11 payload bits**.
For any codepoint in U+00C0–U+00FF (which is exactly where the Portuguese
accented letters live), those top 5 bits are always the same:

```
U+00C0 = 000 1100 0000  -> top 5 = 00011 = 3  -> lead 110 00011 = 0xC3
U+00E7 = 000 1110 0111  -> top 5 = 00011 = 3  -> lead 110 00011 = 0xC3
U+00FF = 000 1111 1111  -> top 5 = 00011 = 3  -> lead 110 00011 = 0xC3
```

The letters differ only in their *low 6 bits*, which live in the **second**
byte. So the first byte is identical for the whole block and all the identity is
in the byte the old code threw away as `§`, `¡`, `£`. Hence one shared symptom
for 26 different letters.

(Codepoints U+0080–U+00BF have top-5 bits `00010` = 2, giving lead byte `0xC2`.
That's the other lead byte you'll see in the wild for Latin-1-range text.)

---

## 5. Reading the decoder, constant by constant

Now the code at [`braille_disc.c:131`](../App/Src/braille_disc.c#L131). Every
constant here is a **marker test** or a **payload mask**, never a letter.

### The two masking idioms

**A "which shape is this byte?" test** looks like `(byte & MASK) == MARKER`.
The mask blanks out the payload so you can compare only the marker bits:

| Test in code | Mask in binary | Reads as | Meaning |
|---|---|---|---|
| `byte < 0x80` | — | is bit 7 clear? | leading `0` → **a complete ASCII character** |
| `(byte & 0xC0u) == 0x80u` | `1100 0000` vs `1000 0000` | look at top **2** bits; are they `10`? | **a continuation byte** — a leftover piece |
| `(byte & 0xE0u) == 0xC0u` | `1110 0000` vs `1100 0000` | look at top **3** bits; are they `110`? | **lead byte of a 2-byte sequence** |
| `(byte & 0xF0u) == 0xE0u` | `1111 0000` vs `1110 0000` | look at top **4** bits; are they `1110`? | **lead byte of a 3-byte sequence** |
| `(byte & 0xF8u) == 0xF0u` | `1111 1000` vs `1111 0000` | look at top **5** bits; are they `11110`? | **lead byte of a 4-byte sequence** |

Notice the mask grows by one bit each row (`0xC0` → `0xE0` → `0xF0` → `0xF8`)
because each shape has one more marker bit than the last.

These five tests are **mutually exclusive and exhaustive**: the markers form a
prefix code, so any byte in 0–255 satisfies exactly one of them (or none, which
is the invalid `11111xxx` case). That's a design property of UTF-8, not luck —
it's why the decoder is a plain `if`/`else if` chain with no ambiguity to
resolve, and why a byte can always be classified on sight.

**A "keep only the payload" mask** is `byte & MASK`, where the mask has a `1`
exactly where the payload bits are — the complement of the marker:

| Mask | Binary | Strips off | Keeps |
|---|---|---|---|
| `0x1Fu` | `0001 1111` | the `110` marker | **5** payload bits from a 2-byte lead |
| `0x0Fu` | `0000 1111` | the `1110` marker | **4** payload bits from a 3-byte lead |
| `0x07u` | `0000 0111` | the `11110` marker | **3** payload bits from a 4-byte lead |
| `0x3Fu` | `0011 1111` | the `10` marker | **6** payload bits from any continuation byte |

So `byte & 0x1Fu` does **not** mean "letter 0x1F". It means: *this is the first
byte of a two-byte letter — throw away the "I am a lead byte" flag and give me
the 5 bits of the letter's ID that it's carrying.*

### The two state variables

```c
static uint32_t utf8_codepoint;      // ID number rebuilt so far
static uint8_t utf8_bytes_remaining; // how many more bytes this letter needs
```

They're `static` because the decoder must **remember across calls** — a letter's
bytes arrive on separate trips through the main loop. `utf8_bytes_remaining == 0`
means "not currently in the middle of a letter."

### The `<< 6` shift

```c
utf8_codepoint = (utf8_codepoint << 6) | (byte & 0x3Fu);
```

Each continuation byte contributes exactly 6 payload bits. So: shift what you
have 6 places left to make room, then OR the new 6 bits into the hole. It's the
same move as building a decimal number digit by digit (`n = n * 10 + digit`),
except the base is 64 instead of 10.

For `ç`:

```
lead byte 0xC3, keep its 5 payload bits:            00011    (=   3)
shift left 6 to make room:                    00011 000000   (= 192)
OR in the 6 payload bits of 0xA7 (= 100111):  00011 100111   (= 231)
                                              └──────┬────┘
                            231 = 0xE7 = U+00E7 = the ç row in the table
```

Compare that with the packing diagram in section 4 and you'll see they are
mirror images of each other: packing splits 11 bits into 5 + 6 and adds markers;
unpacking strips the markers and glues 5 + 6 back together.

### The `UTF8_INCOMPLETE` sentinel

```c
#define UTF8_INCOMPLETE 0xFFFFFFFFu
```

The function must be able to say "no character yet, come back with more bytes."
`0xFFFFFFFF` is not a letter and not a code — it's just a value that can never
be a valid codepoint (Unicode stops at U+10FFFF), so it's safe to reserve as
"nothing to report."

`translate_char_on_disc()` checks for it and **returns without moving the
discs** ([`braille_disc.c:232`](../App/Src/braille_disc.c#L232)). *That* return
is the actual fix: it's what turns two moves per accented letter into one.

---

## 6. Full traces

### `a` — plain ASCII, one byte

| byte | leading bits | branch | result |
|---|---|---|---|
| `0x61` | `0` | ASCII | returns 97 → table row `'a'` → **one move** |

### `ç` — two bytes, one move

| byte | leading bits | branch | state after | returns |
|---|---|---|---|---|
| `0xC3` | `110` | 2-byte lead | codepoint=3, remaining=1 | `UTF8_INCOMPLETE` → **discs don't move** |
| `0xA7` | `10` | continuation | codepoint=231, remaining=0 | **231** → table row `0xE7` = ç → **one move** |

Compare with the old behaviour: same two bytes, but two moves — `Ã` then blank.

### `—` (em dash, U+2014) — three bytes, renders blank

| byte | leading bits | branch | state after | returns |
|---|---|---|---|---|
| `0xE2` | `1110` | 3-byte lead | codepoint=2, remaining=2 | `UTF8_INCOMPLETE` |
| `0x80` | `10` | continuation | codepoint=128, remaining=1 | `UTF8_INCOMPLETE` |
| `0x94` | `10` | continuation | codepoint=8212, remaining=0 | **8212** (= 0x2014) |

8212 is above 255, so it can't index a 256-entry table. The guard in
`translate_char_on_disc()` substitutes `0`, the blank cell:

```c
uint8_t character = (codepoint <= 0xFFu) ? (uint8_t)codepoint : 0u;
```

The alternative — truncating 8212 to a byte — would have given `0x14`, silently
aliasing an em dash onto a random control code. Rendering blank is honest: *this
disc pair has no cell for that character.*

---

## 7. Why malformed input can't wedge the decoder

Three defences, all visible in the code:

1. **A stray continuation byte is dropped.** If a `10xxxxxx` byte shows up when
   `utf8_bytes_remaining == 0`, there's no lead byte to attach it to, so it
   returns `UTF8_INCOMPLETE` and nothing moves. (Before the fix, such a byte
   caused a spurious blank move.)
2. **An ASCII byte always resets the counter.** `utf8_bytes_remaining = 0u` on
   the ASCII path means a truncated sequence — say `0xC3` and then the line
   dropped — doesn't swallow the next real letter. Feed `0xC3` then `'a'` and
   you get one clean `a`.
3. **`0xF8`–`0xFF` are rejected.** Those match none of the four shapes (they'd
   need a 5-or-more-byte form, which UTF-8 never defined), so the counter is
   zeroed and the byte is dropped.

This is why the decoder is *self-synchronising*: at worst you lose the garbled
character, never the stream.

### One call, decided by the leading bits

This flowchart is `utf8_decode_byte()` in picture form — it maps 1:1 onto the
`if`/`else if` chain, in the same order:

```mermaid
flowchart TD
    IN["a byte arrives"] --> ASCII{"leading bit is 0?<br/>i.e. byte below 0x80"}

    ASCII -->|yes| A["a whole ASCII character<br/>clear the counter"]
    A --> RENDER["return the codepoint<br/>DISCS MOVE"]

    ASCII -->|no| CONT{"leading bits are 10?<br/>i.e. a continuation byte"}

    CONT -->|yes| MID{"were we expecting<br/>continuation bytes?"}
    MID -->|"no: stray byte"| DROP["drop it<br/>discs stay put"]
    MID -->|yes| SHIFT["mask off the 10 marker,<br/>shift its 6 payload bits in,<br/>decrement the counter"]
    SHIFT --> DONE{"counter reached 0?"}
    DONE -->|yes| RENDER
    DONE -->|"no: more to come"| WAIT["return UTF8_INCOMPLETE<br/>discs stay put"]

    CONT -->|no| LEAD{"which lead-byte shape?"}
    LEAD -->|"110xxxxx"| L2["keep its 5 payload bits<br/>expect 1 more byte"]
    LEAD -->|"1110xxxx"| L3["keep its 4 payload bits<br/>expect 2 more bytes"]
    LEAD -->|"11110xxx"| L4["keep its 3 payload bits<br/>expect 3 more bytes"]
    LEAD -->|"11111xxx: invalid"| DROP
    L2 --> WAIT
    L3 --> WAIT
    L4 --> WAIT
```

Every path ending in "discs stay put" is a trip through the main loop that
renders nothing. **Before the fix, those paths did not exist** — every byte
reached the table and moved the discs, which is precisely the defect.

Two resync paths are worth spotting in the chart: an **ASCII byte** always takes
the top branch and clears the counter, and a **lead byte** always takes the
bottom branch and restarts the accumulator. Both silently abandon a
half-collected character rather than trying to salvage it.

---

## 8. What this means in practice

**The firmware now expects UTF-8 on the virtual COM port.** That's the right
default: it's what `screen`, `minicom`, `picocom`, PuTTY, and every modern
terminal send by default on a UTF-8 locale, and it's what Python's
`serial.write("ç".encode())` produces.

Note the tradeoff this locks in: the firmware **no longer accepts raw Latin-1**.
If a host sent the single byte `0xE7` for `ç`, the decoder would see `1110 0111`,
read it as a 3-byte lead byte, and swallow the next two bytes. That's an accepted
cost, not an oversight: a lone byte in `0x80`–`0xFF` carries no clue about which
encoding produced it, so the firmware has to commit to one. UTF-8 is the one your
tools actually speak.

To confirm what your terminal is really sending before blaming the firmware:

```bash
locale charmap                # should print UTF-8
printf 'ç' | xxd -g1          # should print c3 a7
```

The echo path in `App_run()` needed no change: it echoes each raw byte as it
arrives, and your terminal reassembles the pair back into `ç` on screen. The
byte stream is passed through untouched in that direction.

**Cost of the whole mechanism:** 5 bytes of RAM (a `uint32_t` accumulator and a
`uint8_t` counter) and a handful of instructions per byte. No allocation, no
buffering of the whole sequence — only the partially-rebuilt number is kept.

---

## 9. Cheat sheet

Read a byte's **leading bits** and you know what it is:

| Leading bits | Byte range | It is | Payload mask |
|---|---|---|---|
| `0` | `0x00`–`0x7F` | a whole ASCII character | none needed |
| `10` | `0x80`–`0xBF` | a continuation byte (piece of a letter) | `0x3F` (6 bits) |
| `110` | `0xC0`–`0xDF` | first of 2 bytes | `0x1F` (5 bits) |
| `1110` | `0xE0`–`0xEF` | first of 3 bytes | `0x0F` (4 bits) |
| `11110` | `0xF0`–`0xF7` | first of 4 bytes | `0x07` (3 bits) |
| `11111` | `0xF8`–`0xFF` | not valid UTF-8 | — |

And the one-line summary of the fix:

> Rebuild the codepoint from its bytes first; only then look up a braille cell.
> Since the table is indexed by Latin-1 — where the index *is* the codepoint for
> U+0000–U+00FF — the rebuilt number drops straight into the existing table with
> no changes to a single row.

---

## Related guides

- **[06-usb-cdc.md](06-usb-cdc.md)** — where these bytes come from: the USB
  interrupt, the ring buffer, and `CDC_ReadChar()`.
- **[02-execution-flow.md](02-execution-flow.md)** — how `App_run()` is reached
  from `main()`.
- **[03-stepper-module.md](03-stepper-module.md)** — what happens *after* the
  character is decoded and an angle is chosen.
