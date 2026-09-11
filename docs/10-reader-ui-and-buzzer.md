# 10. Reader UI and buzzer

This guide covers the part of the firmware a blind reader actually
experiences: how one button press plays a whole labelled value, why the
label letters were chosen the way they were, the four states that make up
the reading model, why there is exactly one pending-package slot, and why
the buzzer is the one module in this design allowed to block.

Files to keep open while reading:

- [App/Src/reader_ui.c](../App/Src/reader_ui.c) — the state machine
- [App/Inc/reader_ui.h](../App/Inc/reader_ui.h) — its contract
- [App/Src/plc_packet.c](../App/Src/plc_packet.c) — the label table
- [App/Src/buzzer.c](../App/Src/buzzer.c) — the five tone patterns
- [App/Src/buttons.c](../App/Src/buttons.c) — debounced button events
- [tests/test_reader_ui.c](../tests/test_reader_ui.c) — every row of 10.3, host-verified

---

## 10.0 Short answers first

| Question | Answer |
|---|---|
| Why does one press read a whole label *and* the value? | So the reader always ends a press with a finger resting on the answer, never mid-label. See 10.1. |
| Why can labels be 1 or 2 letters? | Because a value glyph (all-flat or all-raised) can never be mistaken for a letter still to come — see the invariant in 10.2, and why it is fragile. |
| Why only one pending package, not a queue? | A full read takes tens of seconds; a queue would put the reader further behind the real machine with every field. See 10.4. |
| Why does `buzzer_play()` block? | Because the alternative is a tone that keeps sounding through an unrelated disc move. See 10.5. |
| Has any of this run on real hardware? | `reader_ui.c` is host-tested. The buzzer and the buttons have not run at all. See 10.7. |

---

## 10.1 The reading model: one press plays a whole item

The cell can show exactly one glyph at a time, so a key/value pair — "SMEMA A
IN: true" — has to be a *sequence*: the label's letter (or two), then the
value. `reader_ui.c` treats that sequence as a single unit of navigation
rather than something the reader steps through glyph by glyph:

> "The cell shows one glyph at a time, so a key/value pair is a sequence: the
> label's letter or letters, each held for a dwell, then the value as all
> dots flat (false) or all raised (true). The cell rests on the value, so a
> reader always ends with a finger on the answer."
> — [reader_ui.h:6-9](../App/Inc/reader_ui.h#L6-L9)

One `NEXT` calls `present_current_field()`
([reader_ui.c:81-90](../App/Src/reader_ui.c#L81-L90)), which renders the
field's first label glyph and arms a dwell timer. `reader_ui_tick()` advances
through any remaining label glyphs on its own once each dwell elapses
([reader_ui.c:221-240](../App/Src/reader_ui.c#L221-L240)), and the last step
calls `render_value()`
([reader_ui.c:92-97](../App/Src/reader_ui.c#L92-L97)), which shows the value
glyph and leaves the reader in `RESTING` — no timer running, nothing moving,
until the next button press. The reader never has to track "am I on a letter
or the answer right now": if a finger is on the cell and nothing is
happening, it is on the answer.

Splitting this into a glyph-at-a-time interface (one `NEXT` per letter) was
considered and rejected: it would mean `PREV` and `REPEAT` need to know
whether the reader is mid-label or on a value, for no benefit a blind reader
could use. Showing only the value, with no label at all, was rejected too —
faster, but with nothing to confirm which of the eleven fields is being
read. Playing the whole item is the compromise that keeps the button
semantics simple.

## 10.2 The label table and the invariant that makes it safe

Each of the 11 fields is one row of a fixed table
([plc_packet.c:25-37](../App/Src/plc_packet.c#L25-L37)):

| Field | Label | Why |
|---|---|---|
| SMEMA A IN | `AE` | *entrada* — the SMEMA handshake genuinely needs A/B and in/out |
| SMEMA A OUT | `AS` | *saída* |
| SMEMA B IN | `BE` | |
| SMEMA B OUT | `BS` | |
| SENSOR PRESENÇA | `P` | unambiguous on its own |
| STOP_LINE | `S` | |
| SEND_TIME | `T` | |
| VERDE | `V` | |
| VERMELHO | `R` | ve**R**melho — `V` is already taken by VERDE |
| AMARELO | `M` | a**M**arelo — `A` is already taken by the SMEMA signals |
| released | `L` | *liberado* |

Labels are deliberately **not all the same length.** Uniform two-letter
labels were rejected because every extra letter is another blocking disc
move — for the seven fields with an unambiguous single-letter mnemonic, a
second letter would cost a third of the reading time for no information
gained.

Mixed lengths only work because of one invariant, stated in the module's own
doc comment:

> "Labels are one or two letters. That is safe only because a value renders
> as all-flat or all-raised and no label letter is either, so the value
> always announces itself as the end of the sequence."
> — [plc_packet.h:11-14](../App/Inc/plc_packet.h#L11-L14)

The two value glyphs are `BRAILLE_DOTS_ALL_FLAT` (`0x00`) and
`BRAILLE_DOTS_ALL_RAISED` (`0x3F`)
([braille_disc.h:75-77](../App/Inc/braille_disc.h#L75-L77)). As long as no
label letter's dot pattern equals either of those, a reader feeling the cell
never has to guess whether the next glyph continues the label or *is* the
answer: a letter means "keep going," flat-or-full means "you have arrived."
`test_plc_packet.c` asserts this for every glyph of every field
([test_plc_packet.c:59-70](../tests/test_plc_packet.c#L59-L70)).

**This is fragile, not automatic.** The Brazilian Braille table this project
draws from includes at least one letter that *is* all six dots:

> `[0xC9] = DOT1 | DOT2 | DOT3 | DOT4 | DOT5 | DOT6, // E-acute   (E)`
> — [braille_disc.c:76](../App/Src/braille_disc.c#L76)

`É` (E-acute) renders identically to `BRAILLE_DOTS_ALL_RAISED` — a "true"
value. If a future edit ever added `É` as a label letter (a plausible choice:
several of the PLC field names contain accented vowels), a field whose label
ended in `É` would be indistinguishable from a field whose *value* had just
been reached, and the sequencer would look broken with no obvious cause.
**`É` must never appear in `plc_fields`.** The host test above exists
precisely to catch this before it reaches hardware.

## 10.3 The four states and the full event table

```
NO_DATA --NEXT (asks PLC)--> AWAITING_POLL --snapshot--> LABEL --dwell out, more glyphs--> LABEL
                                    |                        |--dwell out, last glyph-->  RESTING
                                    |--link fault (no pkg)-->  NO_DATA
RESTING --NEXT/PREV/REPEAT--> LABEL
LABEL   --PREV/REPEAT-------> LABEL (re-presents from glyph 0)
```

The four states ([reader_state_t](../App/Src/reader_ui.c#L30-L35)):

- **`NO_DATA`** — no package has ever been adopted; the cell is blank.
- **`AWAITING_POLL`** — an explicit, out-of-cycle request is in flight.
- **`LABEL`** — showing label glyph *n* of the current field; a dwell timer
  is running.
- **`RESTING`** — holding a package, idle on a value (or, at `cursor == -1`,
  holding a package that has never been stepped into).

Button events arrive already debounced and coalesced from `buttons.c` — each
button carries one flag rather than a counter, so three impatient taps during
one blocking disc move collapse into a single advance
([buttons.c:1-12](../App/Src/buttons.c#L1-L12)). The very first thing
`reader_ui_on_event()` does is drop the event entirely if a request is
outstanding
([reader_ui.c:134-140](../App/Src/reader_ui.c#L134-L140)): acting on a stale
press after the answer has already landed would move the reader somewhere
they asked to go a second ago, not where they are now.

| Event | Guard | What happens |
|---|---|---|
| `NEXT` | no package held | request a poll, play `REQUEST_SENT`, enter `AWAITING_POLL` |
| `NEXT` | mid-package | advance the cursor, present the next field |
| `NEXT` | at the last field, pending package waiting | adopt the pending package (and clear the slot), present field 0 of the new package |
| `NEXT` | at the last field, nothing pending | force an out-of-cycle poll (same as "no package held") |
| `PREV` | `cursor > 0` | step back, re-present from glyph 0 |
| `PREV` | at field 0 or before reading started | `BOUNDARY` blip; discs do not move |
| `REPEAT` | reading has started | re-present the current field from glyph 0 |
| `REPEAT` | before reading has started | `BOUNDARY` blip |
| a poll answers | reader was `AWAITING_POLL` | adopt it as the answer regardless of whether it differs from the old package |
| a poll answers | reader had no package yet | adopt without rendering, play `DATA_RECEIVED`; `cursor = -1` so the cell stays blank until the first `NEXT` shows field 0 |
| a poll answers | differs, and nothing was pending | fill the pending slot, play `PACKAGE_QUEUED` (rate-limited) |
| a poll answers | differs, and something was already pending | overwrite it, **silently** — see 10.4b |
| a poll answers | identical to the current package | **clear the pending slot**, no sound |
| link fault | any state | play `LINK_FAULT` (rate-limited); if `AWAITING_POLL`, fall back to `RESTING` (or `NO_DATA` with nothing held) rather than resuming whatever was showing before |
| tick | `LABEL`, dwell elapsed, more glyphs left | render the next glyph, re-arm the dwell |
| tick | `LABEL`, dwell elapsed, last glyph | render the value, enter `RESTING` |
| tick | `AWAITING_POLL`, request timed out | treat it as a link fault |

Full implementation:
[reader_ui_on_event()](../App/Src/reader_ui.c#L134-L177),
[reader_ui_on_snapshot()](../App/Src/reader_ui.c#L179-L206),
[reader_ui_on_link_fault()](../App/Src/reader_ui.c#L208-L219),
[reader_ui_tick()](../App/Src/reader_ui.c#L221-L253).

The "fall back to `RESTING`, not the previous state" rule on a link fault
during `AWAITING_POLL` deserves a second look, because the obvious
alternative is wrong: whatever state was showing before the out-of-cycle
request was sent had a dwell timer that stopped being serviced the moment the
request went out. Resuming it now would mean the sequence appears to move on
its own — a glyph changing with no button press — rather than simply holding
still until the reader presses something again
([reader_ui.c:212-218](../App/Src/reader_ui.c#L212-L218)).

## 10.4b The announcement is an edge, not a level

"Newer data is waiting" is one bit, and the reader learns it once. Announcing
it per poll would mean announcing it twice a second for as long as the slot
stayed full — and on a device whose reader is blind, that competes with the
reading itself for the only channel there is. So `PACKAGE_QUEUED` plays when
the slot goes from empty to full, and not again while it stays full. Newer
values still replace older ones in the slot; they just do it quietly, because
there is nothing further to learn from hearing about them.

Adoption is what re-arms it, which is why the spacing follows the reader's own
pace rather than a timer.

A poll matching what the reader already holds **clears** the slot. That is not
housekeeping: what the slot holds at that moment is the intermediate value the
line has since left, and keeping it would let a later step past the last field
adopt a package the line no longer shows — announced by the confirmation tone,
with nothing to give it away as stale.

Clearing on equality also means a flapping line can re-arm the edge at the
polling rate, which is the job `PLC_QUEUED_SOUND_MIN_INTERVAL_MS` now does. It
is no longer what spaces announcements out; it is a backstop against a
chattering signal, which is the role the design always described for it.

## 10.4 One pending slot, not a queue

`reader_ui.c` keeps exactly one pending package
([reader.pending](../App/Src/reader_ui.c#L54-L55)); a newer poll result
simply overwrites it. The reason is arithmetic, not a memory-saving instinct:

- The package has 11 fields. Four of them (the SMEMA signals) carry
  two-letter labels, the other seven carry one letter — 15 label glyphs
  total. At `PLC_LABEL_DWELL_MS` = 1500&nbsp;ms
  ([plc_config.h:75](../App/Inc/plc_config.h#L75)), a full walk from field 0
  to field 10 costs on the order of tens of seconds of dwell time alone,
  before counting a disc move before every glyph.
- The link polls every `PLC_POLL_INTERVAL_MS` = 500&nbsp;ms
  ([plc_config.h:82](../App/Inc/plc_config.h#L82)) — roughly 90 polls during
  one full read.

A FIFO queue of pending packages would mean that by the time a reader
finished an 11-field walk during which the machine kept running, they would
be sitting on a queue of packages representing history, each one further
behind the PLC's actual current state than the last — reading the past
instead of the present, with the gap growing every time they pressed `NEXT`.
Overwriting instead means the *opposite* guarantee: whichever package is
pending when the reader finally reaches the last field is, by construction,
the newest one a poll has seen. Stepping past the last field
([reader_ui.c:106-114](../App/Src/reader_ui.c#L106-L114),
[reader_ui.c:146-153](../App/Src/reader_ui.c#L146-L153)) always lands on the
current state of the line, never on a snapshot from a minute ago.

The header comment states this as one of the two rules in the file that are
"deliberate and easy to undo by accident"
([reader_ui.c:7-12](../App/Src/reader_ui.c#L7-L12)) — worth remembering
before turning the single slot back into a queue to "not lose data": nothing
is lost by overwriting a pending package, because the reader was never going
to read it before an even newer one arrived anyway.

## 10.5 Why the buzzer blocks, and the five patterns

Every other cooperative module in this firmware returns immediately and gets
ticked again next pass. `buzzer_play()` is the one deliberate exception:

> "PWM keeps sounding in hardware with no software involvement, so a tone
> started immediately before a blocking disc move would sound for the whole
> move unless something stopped it."
> — [buzzer.h:42-44](../App/Inc/buzzer.h#L42-L44)

A disc move already blocks for up to about a second
([08-motion-and-homing.md](08-motion-and-homing.md)); if a tone kept playing
underneath that move instead of finishing first, a reader would hear a sound
stretched to an unpredictable length that has nothing to do with the pattern
that was meant to play. `buzzer_play()` sidesteps the problem entirely by
looping over the pattern's steps and calling `HAL_Delay()` for each one
([buzzer.c:153-163](../App/Src/buzzer.c#L153-L163)) — the longest pattern is
480&nbsp;ms, which is short next to a disc move that already blocks for
around a second, and button presses are latched by their EXTI callback
throughout, so nothing is lost while the buzzer is busy.

The five patterns
([buzzer.c:62-97](../App/Src/buzzer.c#L62-L97)), separated by both pitch and
texture so they stay apart on a small piezo:

| Sound | Pattern | Total | Meaning |
|---|---|---|---|
| `REQUEST_SENT` | 880&nbsp;Hz 60&nbsp;ms, gap, 1175&nbsp;Hz 60&nbsp;ms | 160&nbsp;ms | rising pair — the device asked the PLC something |
| `DATA_RECEIVED` | 1568&nbsp;Hz 150&nbsp;ms | 150&nbsp;ms | one longer, higher tone — the answer is now what you're reading |
| `PACKAGE_QUEUED` | 2093&nbsp;Hz 40&nbsp;ms × 3 | 220&nbsp;ms | fast triple chirp — newer data is waiting behind this package |
| `LINK_FAULT` | 220&nbsp;Hz 200&nbsp;ms × 2 | 480&nbsp;ms | slow low double — deliberately the longest, so it is unmistakable |
| `BOUNDARY` | 330&nbsp;Hz 40&nbsp;ms | 40&nbsp;ms | short low blip — nothing there: already at field 0, or nothing to repeat |

Pitch alone was rejected as the only way to tell sounds apart: two tones that
differ only in frequency are hard to distinguish on a small piezo speaker in
a noisy environment, so "asking" is a rising pair, "received" is a single
longer tone, and "something is waiting" is a fast chirp — a shape as well as
a pitch. `buzzer_tone()` resets the timer's counter before starting each
step ([buzzer.c:135-146](../App/Src/buzzer.c#L135-L146)), so a step always
begins at the start of a PWM cycle and never emits a truncated first pulse.

## 10.6 Why `reader_ui` takes an IO struct

`reader_ui.c` never includes `buzzer.h`, `plc_link.h`, or any STM32 header.
Everything it needs from the outside world is five function pointers,
supplied once at init:

```c
typedef struct {
  void (*render_char)(uint8_t latin1_char);
  void (*render_dots)(uint8_t dots);
  void (*play_sound)(reader_sound_t sound);
  void (*request_poll)(void);
  uint32_t (*now_ms)(void);
} reader_ui_io_t;
```
— [reader_ui.h:56-62](../App/Inc/reader_ui.h#L56-L62)

This is the same seam described in
[01-architecture-layers.md](01-architecture-layers.md#why-the-core--port-split-twice)
for `Lib/tmc2209` and `App/stepper` — logic on one side that does not know
what a chip is, a struct of function pointers as the only door through to
hardware — applied here for a third reason beyond portability: it lets
`reader_ui_on_event()`, `reader_ui_on_snapshot()` and `reader_ui_tick()` be
driven from a host test with **zero** of the code under test ever touching a
peripheral. On the actual board, wiring the seam back up costs nothing extra:
every real implementation's signature already matches the pointer type it
fills, so `app_main.c` is just two tables, no adapter functions
([app_main.c:65-79](../App/Src/app_main.c#L65-L79)):

```c
static const reader_ui_io_t app_reader_io = {
    .render_char = braille_render_char,
    .render_dots = braille_render_dots,
    .play_sound = buzzer_play,
    .request_poll = plc_link_request_now,
    .now_ms = HAL_GetTick,
};
```

### A worked example: reading `tests/test_reader_ui.c`

The suite's recording IO turns every side effect into one character, and
compares whole sequences as strings — the file this legend is defined at the
top of is exactly what a future maintainer needs to read before changing
navigation behaviour:

```
A B E S P T V R M L   a label glyph was rendered (the letter itself)
-                     value glyph, all dots flat  (false)
#                     value glyph, all dots raised (true)
@                     a poll was requested
? ! * x .             REQUEST_SENT DATA_RECEIVED PACKAGE_QUEUED
                       LINK_FAULT BOUNDARY
```
— [test_reader_ui.c:1-14](../tests/test_reader_ui.c#L1-L14)

So `"AE-"` reads as "showed label `A`, then `E`, then rested on a false
value" — one complete item for SMEMA A IN. Reading a whole assertion in the
suite now decodes directly:

```c
start(0x0000u);
walk_to(LAST_INDEX);
CHECK_STR_EQ(log_buf, "AE-AS-BE-BS-P-S-T-V-R-M-L-");
```
— [test_reader_ui.c:147-149](../tests/test_reader_ui.c#L147-L149)

`start(0x0000u)` adopts an all-false package with no rendering logged yet;
`walk_to(LAST_INDEX)` presses `NEXT` once per field and lets each item's
dwell run out. The resulting log is eleven items back to back, each ending
in `-` because every coil is `0` in this snapshot — `AE-`, `AS-`, `BE-`,
`BS-`, then the seven single-letter fields each followed by their own `-`.
This single string is simultaneously an assertion about the reading *order*
(coil order, pinned by the table in 10.2) and about the *content* of every
glyph, without a single mock object or hardware peripheral involved.

## 10.7 What has actually been verified

`reader_ui.c` is pure logic and is host-tested: 39 checks in
`test_reader_ui`, part of 261 checks across four suites
(`braille_disc`, `plc_packet`, `modbus_tcp`, `reader_ui`) that run on a
development machine with no STM32 toolchain involved. Every row of the event
table in 10.3, the pending-slot overwrite behaviour, the dwell sequencing,
and the rate limiters have each been exercised this way.

**`buzzer.c` and `buttons.c` have had no execution at all.** Both need real
hardware to mean anything: the buzzer needs a real TIM4/PWM output and an
actual piezo to confirm the five patterns are audible and distinguishable
from each other, and the buttons need real EXTI edges — including real
contact bounce — to confirm the debounce lockout actually produces one event
per press. Nothing about either module has been run, on the bench or
otherwise; what stands behind 10.3's button-coalescing claim and 10.5's
pattern table is static review of the code, not a test run. The checklist in
[11-hardware-bring-up.md](11-hardware-bring-up.md) covers both before
either is trusted in the field.

---

**Previous:** [09-modbus-tcp-and-plc-link.md](09-modbus-tcp-and-plc-link.md) ·
**Next:** [11-hardware-bring-up.md](11-hardware-bring-up.md) ·
**Index:** [README.md](README.md)
