# 10. Sound feedback

The operator cannot see the cell or the device, so every state change that is
not felt on the cell is heard. This document describes the buzzer hardware,
how tones are generated, the five sound patterns and when each plays, the rate
limits, and how to add a sound.

**Source files:** `App/Src/buzzer.c`, `App/Inc/buzzer.h`,
`App/Src/reader_ui.c`, `App/Inc/reader_ui.h` (`reader_sound_t`),
`App/Inc/plc_config.h`, `Core/Src/tim.c` (`MX_TIM4_Init()`).

## 10.1 Hardware

A **passive** piezo buzzer, switched on its low side by a BC368 NPN transistor:

| Connection | To |
|---|---|
| PB6 (TIM4_CH1, AF push-pull) | 1 kΩ → BC368 base |
| BC368 emitter | GND |
| BC368 collector | buzzer − |
| buzzer + | +5 V from the power breakout |

PB6 high turns the transistor on and drives current through the piezo, so the
PWM signal on PB6 appears inverted on the buzzer's − terminal and the piezo
sees a 5 V square wave at the PWM frequency. The firmware does not depend on
the polarity: only the frequency and 50 % duty matter. The buzzer must be
passive; an active buzzer (with its own oscillator) would only click.

## 10.2 Tone generation

TIM4 is configured by `MX_TIM4_Init()` with prescaler 47 and **auto-reload
preload disabled**. `buzzer_init(htim, channel)` stores the handle and
`TIM_CHANNEL_1` (`APP_BUZZER_TIM_CHANNEL` in `app_main.c`) and computes the
counter tick from the clock tree with `buzzer_timer_tick_hz()`: PCLK1 × 2
(APB1 prescaler ≠ 1) ÷ (PSC + 1) = 24 MHz × 2 / 48 = 1 MHz.

For each tone, `buzzer_tone(frequency_hz)` writes:

- `ARR = tick / frequency − 1`;
- `CCR1 = (ARR + 1) / BUZZER_DUTY_DIVISOR` (50 % duty);
- the counter to 0, then starts PWM.

Because ARR preload is off, a new period takes effect immediately rather than
at the next update event, and resetting the counter means each step starts on a
full cycle. A frequency of 0 (`BUZZER_TONE_SILENT_HZ`) stops the PWM for that
step. TIM4 is a 16-bit timer: at a 1 MHz tick the lowest expressible tone is
about 15.3 Hz, far below any pattern.

### Why `buzzer_play()` blocks

`buzzer_play()` plays the whole pattern with `HAL_Delay()` between steps and
returns when it is silent. PWM keeps sounding in hardware without software
involvement, so a tone started just before a blocking disc move would last for
the whole move unless something stopped it. Every pattern is at most 480 ms;
see the blocking budget in
[03-execution-flow.md](03-execution-flow.md#37-blocking-budget).

## 10.3 The five patterns

The patterns are distinguished by texture as well as pitch: two sounds that
differ only in frequency are hard to tell apart on a small piezo.

| Sound (`reader_sound_t`) | Steps | Total | Meaning |
|---|---|---|---|
| `READER_SOUND_REQUEST_SENT` | 880 Hz 60 ms · silence 40 ms · 1175 Hz 60 ms | 160 ms | rising pair: the device asked the PLC for data |
| `READER_SOUND_DATA_RECEIVED` | 1568 Hz 150 ms | 150 ms | one longer, higher tone: a package arrived and is now the one being read (or is ready to read) |
| `READER_SOUND_PACKAGE_QUEUED` | 2093 Hz 40 ms × 3, separated by 50 ms | 220 ms | fast triple chirp: newer data is waiting behind the package being read |
| `READER_SOUND_LINK_FAULT` | 220 Hz 200 ms · silence 80 ms · 220 Hz 200 ms | 480 ms | slow low double: the link is not delivering; deliberately the longest pattern |
| `READER_SOUND_BOUNDARY` | 330 Hz 40 ms | 40 ms | short low blip: nothing there (PREV at field 0, or REPEAT/PREV before reading started) |

Frequencies and durations are the `BUZZER_TONE_*_HZ` and `BUZZER_*_MS`
constants in `buzzer.c`. When each sound plays is listed in the reader's event
table, [09-reader-ui.md](09-reader-ui.md#94-event-table).

## 10.4 Rate limits

Two sounds can be triggered repeatedly by the network rather than by the
operator. Without limits, an unplugged cable or a chattering signal would sound
continuously and mask the sounds that matter.

| Sound | Minimum spacing | Constant |
|---|---|---|
| `LINK_FAULT` | 10 s | `PLC_FAULT_SOUND_MIN_INTERVAL_MS` |
| `PACKAGE_QUEUED` | 5 s | `PLC_QUEUED_SOUND_MIN_INTERVAL_MS` |

Rate limiting is done in `reader_ui.c` (`rate_limit_allows()`), not in the
link: the link reports every fault. The first occurrence after boot always
plays. Suppressed sounds are dropped, not delayed. `PACKAGE_QUEUED` is in
addition announced only when the pending slot goes from empty to full
([09-reader-ui.md](09-reader-ui.md#95-one-pending-slot-not-a-queue)).

What the operator hears in common situations:

| Situation | Sound |
|---|---|
| Cable unplugged, or PLC unreachable | `LINK_FAULT` about every 10 s |
| W5500 missing at boot | `LINK_FAULT` once (plus LED blink code 3) |
| PLC answers normally, nothing changes | nothing |
| A signal changes while reading | `PACKAGE_QUEUED` once |

## 10.5 Adding a sound

1. Add a value to `reader_sound_t` in `reader_ui.h`.
2. In `buzzer.c`, define its tones and durations as named constants, a
   `buzzer_steps_<name>[]` array of `buzzer_step_t`, a
   `buzzer_pattern_<name>` with `BUZZER_PATTERN(<name>)`, and a case in
   `buzzer_pattern_for()`.
3. Keep it short (it blocks the loop) and distinct in texture from the
   existing five, then play it from `reader_ui.c` with `play()`.
4. Give it a character in the recording fake of `tests/test_reader_ui.c` and
   extend the expected sequences; run `make -C tests`.
