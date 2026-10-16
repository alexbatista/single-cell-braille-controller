# 9. Reader UI and buttons

This document describes how the device presents a package of PLC signals on a
single cell and how the operator moves through it: the reading model, the label
rules, the reader's states and the full event table, the single pending-package
slot, and the three buttons. The sounds the reader triggers are in
[10-sound-feedback.md](10-sound-feedback.md).

**Source files:** `App/Src/reader_ui.c`, `App/Inc/reader_ui.h`,
`App/Src/plc_packet.c`, `App/Inc/plc_packet.h`, `App/Src/buttons.c`,
`App/Inc/buttons.h`, `App/Src/app_main.c`, `tests/test_reader_ui.c`,
`tests/test_buttons.c`, `tests/test_plc_packet.c`.

## 9.1 Reading model

A package is a list of key–value pairs (11 in the shipped configuration). The
cell shows one glyph at a time, so each pair is presented as a sequence:

1. the **label**, one or two letters, each held for `PLC_LABEL_DWELL_MS`
   (1500 ms) after the discs arrive;
2. the **value**: all dots flat (`false`) or all dots raised (`true`).

One press of NEXT plays a whole pair. The cell then rests on the value until
the next press, so whenever the cell is still, the reader's finger is on an
answer, never in the middle of a label. The dwell starts after the render
returns, i.e. once the glyph is physically in place.

Reading time for the shipped package: 15 label letters × 1.5 s = 22.5 s of
dwell, plus 26 disc transitions (15 letters + 11 values) of at most 0.5 s each
= 13 s, so at most about 36 s plus the time the operator rests on each value.

## 9.2 States

`reader_state_t` in `reader_ui.c`:

| State | Meaning |
|---|---|
| `READER_STATE_NO_DATA` | No package held; cell blank. |
| `READER_STATE_AWAITING_POLL` | The reader asked the link for a package and is waiting; button events are ignored. |
| `READER_STATE_LABEL` | Showing label glyph `glyph_index` of field `cursor`; the dwell timer is running. |
| `READER_STATE_RESTING` | Holding a package and idle: on a value, or (cursor = −1) holding a package not yet read. |

Other state: `cursor` (field index, −1 before reading starts), `current` (the
package being read), and one `pending` slot for a newer package.

## 9.3 Labels

The label table is `plc_fields[]` in `plc_packet.c`, in reading order:

| Coil | Signal | Label | Mnemonic |
|---|---|---|---|
| 0 | SMEMA A in | `AE` | *entrada* |
| 1 | SMEMA A out | `AS` | *saída* |
| 2 | SMEMA B in | `BE` | |
| 3 | SMEMA B out | `BS` | |
| 4 | Presence sensor | `P` | *presença* |
| 5 | Stop line | `S` | |
| 6 | Send time | `T` | |
| 7 | Green lamp | `V` | *verde* |
| 8 | Red lamp | `R` | *veRmelho* (V is taken) |
| 9 | Yellow lamp | `M` | *aMarelo* (A is taken) |
| 10 | Released | `L` | *liberado* |

Rules for any label table:

- 1 or 2 letters (`PLC_LABEL_MAX_GLYPHS`). Use one letter whenever it is
  unambiguous: each extra letter costs a disc move and a dwell.
- **No label letter may render as all-flat or all-raised.** The value glyph is
  what marks the end of a label; a letter with the same pattern would be read
  as a value. In the current glyph table this excludes the space and **É**.
  `tests/test_plc_packet.c` checks every label against
  `braille_pattern_for_char()`.
- Only letters present in the glyph table can be used
  ([06-braille-encoding.md](06-braille-encoding.md#63-glyph-table)).

## 9.4 Event table

Events come from three sources: buttons (`reader_ui_on_event()`), the link
(`reader_ui_on_snapshot()`, `reader_ui_on_link_fault()`) and time
(`reader_ui_tick()`). Sounds are named by their `reader_sound_t` value.

| Event | Condition | Effect |
|---|---|---|
| any button | state is `AWAITING_POLL` | ignored |
| NEXT | no package held | request a poll (`request_poll`), play `REQUEST_SENT`, enter `AWAITING_POLL` |
| NEXT | cursor before the last field | cursor + 1, show its first label glyph |
| NEXT | on the last field, `pending` holds a package | adopt `pending` (clear the slot), play `DATA_RECEIVED`, show field 0 |
| NEXT | on the last field, `pending` empty | request a poll, as when no package is held |
| PREV | package held and cursor > 0 | cursor − 1, show its first label glyph |
| PREV | otherwise | play `BOUNDARY` |
| REPEAT | package held and cursor ≥ 0 | show the current field again from its first glyph |
| REPEAT | otherwise | play `BOUNDARY` |
| snapshot | state is `AWAITING_POLL` | adopt it (even if identical to `current`), play `DATA_RECEIVED`, show field 0 |
| snapshot | no package held yet | store it as `current`, cursor −1, play `DATA_RECEIVED`, cell stays blank, enter `RESTING`; the first NEXT shows field 0 |
| snapshot | equal to `current` | clear `pending`; no sound |
| snapshot | differs, `pending` empty | store in `pending`, play `PACKAGE_QUEUED` (rate-limited) |
| snapshot | differs, `pending` full | overwrite `pending`, no sound |
| link fault | any state | play `LINK_FAULT` (rate-limited); if `AWAITING_POLL`, go to `RESTING` (or `NO_DATA` if nothing is held). The held package is never discarded. |
| tick | `LABEL`, dwell elapsed, more label glyphs | show the next glyph, restart the dwell |
| tick | `LABEL`, dwell elapsed, last glyph shown | show the value, enter `RESTING` |
| tick | `AWAITING_POLL`, `PLC_RESPONSE_TIMEOUT_MS` elapsed | handled as a link fault |

NEXT, PREV and REPEAT also act during `LABEL`: they interrupt the running label
and start the target field from its first glyph.

## 9.5 One pending slot, not a queue

Changes on the PLC while a package is being read are not queued. The single
`pending` slot always holds the newest differing package:

- A full read takes tens of seconds. A queue would put the reader further
  behind the machine with every change; stepping past the last field always
  lands on the machine's current state.
- If the line returns to the state being read, the slot is cleared. Otherwise
  the slot would keep an intermediate state the line has already left, and
  stepping past the last field would adopt a stale package.
- "Newer data is waiting" is announced once, when the slot goes from empty to
  full. Later differing polls overwrite the slot silently, because the reader
  learns nothing new from hearing it again. The rate limit
  (`PLC_QUEUED_SOUND_MIN_INTERVAL_MS`, 5 s) remains as a backstop: a line that
  flaps between two states can empty and refill the slot at the polling rate.

## 9.6 Buttons

| Button | Pin | Event |
|---|---|---|
| NEXT | PB8 | `READER_EV_NEXT` |
| PREV | PB9 | `READER_EV_PREV` |
| REPEAT | PB7 | `READER_EV_REPEAT` |

Each button closes its pin to GND; the internal pull-up is the only resistor.
The pins interrupt on the falling edge on the shared `EXTI9_5_IRQn`, together
with the W5500 INT pin (PB5). `HAL_GPIO_EXTI_Callback()` in `app_main.c` offers
every edge to both `buttons_on_exti()` and `plc_link_on_exti()`; each ignores
pins it does not own.

### Debouncing: first edge wins, the release re-arms

There is no hardware debounce, and a mechanical contact bounces on release as
well as on press. With falling-edge interrupts, release bounce produces new
falling edges at a time the operator chooses (however long the button was
held), so no fixed lockout window measured from the press can cover it.
`buttons.c` therefore works like this:

1. **Press (ISR, `buttons_on_exti()`):** every edge stamps `last_edge_ms`. If
   the button is armed, it is disarmed and `pending` is set. The first edge is
   the press: no added latency.
2. **Re-arm (main loop, `buttons_take_event()` → `rearm_released_buttons()`):**
   a disarmed button re-arms only when its pin reads released (high) **and** no
   edge has been seen for `PLC_BUTTON_RELEASE_STABLE_MS` (40 ms). Both are
   needed: the level alone would re-arm in a gap inside a bounce burst; the
   quiet time alone would re-arm a button still held down. The check runs with
   interrupts masked so an edge cannot land between reading the pin and arming.
3. **Delivery (`buttons_take_event()`):** returns at most one event per call,
   checking NEXT, PREV, REPEAT in that order. Read-and-clear of `pending` is
   done with interrupts masked, restoring the previous PRIMASK.

Each button holds one flag, not a counter: three taps of NEXT during one disc
move advance one field. Behaviour during blocking calls is described in
[03-execution-flow.md](03-execution-flow.md#38-button-presses-during-a-block).

## 9.7 Testing seam

`reader_ui` reaches the outside world only through `reader_ui_io_t`:

| Callback | Production binding |
|---|---|
| `render_char` | `braille_render_char` |
| `render_dots` | `braille_render_dots` |
| `play_sound` | `buzzer_play` |
| `request_poll` | `plc_link_request_now` |
| `now_ms` | `HAL_GetTick` |

`tests/test_reader_ui.c` binds recording fakes and a controllable clock, and
compares whole output sequences encoded one character per effect:

| Character | Effect |
|---|---|
| `A B E S P T V R M L` | that label letter rendered |
| `-` / `#` | value rendered: all flat / all raised |
| `@` | poll requested |
| `?` `!` `*` `x` `.` | sounds `REQUEST_SENT`, `DATA_RECEIVED`, `PACKAGE_QUEUED`, `LINK_FAULT`, `BOUNDARY` |

So `"AE-"` is one complete item: label A, then E, then a `false` value.
`tests/test_buttons.c` exercises the debounce logic with a stubbed HAL
(`tests/stubs/`).
