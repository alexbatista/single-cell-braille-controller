# 11. Hardware bring-up checklist

Everything else in these guides describes intended behaviour, verified by 261
host-test checks across four suites (`braille_disc`, `plc_packet`,
`modbus_tcp`, `reader_ui`) that exercise pure logic on a development machine.
**None of this firmware has run on real hardware.** No board has been
connected to a W5500, no W5500 has been connected to a PLC, and `plc_link.c`,
`buzzer.c`, `buttons.c`, and the W5500 SPI port
(`Lib/w5500/w5500_stm32.c`) have had no execution at all — only static
review. See [09-modbus-tcp-and-plc-link.md §9.8](09-modbus-tcp-and-plc-link.md#98-what-has-actually-been-verified)
and [10-reader-ui-and-buzzer.md §10.7](10-reader-ui-and-buzzer.md#107-what-has-actually-been-verified)
for exactly what each guide's claims rest on.

This is the list of what remains, in the order to attempt it. The order is
deliberate: each step is cheaper to diagnose than the one after it, and each
one assumes the step before it already works — there is no point chasing a
button bounce if the buzzer that is supposed to confirm the press has never
made a sound.

- [ ] **1. W5500 SPI.** Power the board with the W5500 attached. On every
  bring-up and reconnect, `chip_bring_up()` reads the chip's `VERSIONR`
  register and expects `0x04`
  ([plc_link.c:106-108](../App/Src/plc_link.c#L106-L108)) — every real W5500
  answers with that value, so this single read exercises chip select, clock
  polarity, bit order and wiring all at once, before any of it is buried
  under the connection state machine. Success sounds like the link
  eventually reaching `CONNECTING`/`IDLE` rather than cycling `CHIP_FAULT`;
  failure sounds like the `LINK_FAULT` pattern repeating with LED blink code
  3 (`FAULT_LED_CODE_ETHERNET`,
  [fault_led.h:32](../App/Inc/fault_led.h#L32)) every
  `PLC_RECONNECT_DELAY_MS`. If it fails, work the ladder in
  [09-modbus-tcp-and-plc-link.md §9.9](09-modbus-tcp-and-plc-link.md#99-troubleshooting-proving-the-spi-link):
  `ETH_NSS` (PB12) toggling, `ETH_RESET` (PA10) releasing, SPI2 mode 0
  MSB-first, and dropping the 12 Mbit/s prescaler to 6 Mbit/s if the jumper
  leads are long.

- [ ] **2. Buzzer.** Trigger all five patterns
  (`REQUEST_SENT`, `DATA_RECEIVED`, `PACKAGE_QUEUED`, `LINK_FAULT`,
  `BOUNDARY` — table in
  [10-reader-ui-and-buzzer.md §10.5](10-reader-ui-and-buzzer.md#105-why-the-buzzer-blocks-and-the-five-patterns))
  and confirm each one is distinguishable from the others by ear, not just on
  paper — the rising pair, the single tone and the triple chirp are close in
  pitch and this is the first time any of them will have driven a real piezo.
  Confirm nothing is left sounding once a pattern finishes; `buzzer_tone()`
  explicitly silences the timer output at the end of every pattern
  ([buzzer.c:162](../App/Src/buzzer.c#L162)), so a tone still audible after
  that is a hardware or wiring problem, not a firmware one.

- [ ] **3. Buttons.** Press `BTN_NEXT`, `BTN_PREV` and `BTN_REPEAT`
  individually and confirm: exactly one event per press, always the correct
  button, no double-trigger from contact bounce within the
  `PLC_BUTTON_DEBOUNCE_LOCKOUT_MS` (40&nbsp;ms,
  [plc_config.h:101](../App/Inc/plc_config.h#L101)) window, and a press held
  down fires once rather than repeating. `buttons.c`'s debounce is
  first-edge-wins with a lockout, not a timer that waits for the signal to
  settle before accepting anything
  ([buttons.c:1-12](../App/Src/buttons.c#L1-L12)) — this step is what
  confirms that design choice actually holds on the real switches, not just
  in the state diagram of [10-reader-ui-and-buzzer.md §10.3](10-reader-ui-and-buzzer.md#103-the-four-states-and-the-full-event-table).

- [ ] **4. End to end**, against a running PLC (or simulator) serving the
  same 11-coil package this firmware expects:
  - [ ] Boot the board and confirm the first background poll succeeds —
    listen for `DATA_RECEIVED` with no button pressed, which only happens on
    the very first snapshot ever adopted
    ([reader_ui.c:186-193](../App/Src/reader_ui.c#L186-L193)).
  - [ ] Walk all eleven fields with `NEXT`, cross-checking every value glyph
    against the PLC simulator's actual coil states, not just against what
    the firmware itself reports.
  - [ ] Exercise `PREV`, `REPEAT`, and the boundary case at both ends of the
    package (before field 0, and repeating with nothing read yet).
  - [ ] Trigger an out-of-cycle request by pressing `NEXT` at the last field
    with nothing pending, and confirm it plays `REQUEST_SENT` then
    `DATA_RECEIVED` rather than silently doing nothing.
  - [ ] Change a coil on the PLC mid-read and confirm the `PACKAGE_QUEUED`
    chirp plays, then confirm stepping past the last field jumps straight to
    the newer package rather than replaying the old one.
  - [ ] Unplug the Ethernet cable mid-session and confirm recovery: the
    `LINK_FAULT` pattern (rate-limited, not continuous), the package already
    held staying available for `PREV`/`REPEAT`, and a clean reconnect once
    the cable is replugged.
  - [ ] Power up with the W5500 physically absent and confirm the device
    still boots, still drives the discs and buttons, and reports the
    Ethernet fault (blink code 3, rate-limited `LINK_FAULT`) rather than
    hanging.
  - [ ] Tap a button repeatedly during a blocking disc move and confirm the
    presses coalesce into one advance rather than skipping fields — the
    behaviour `buttons.c`'s single-flag-per-button design is supposed to
    guarantee ([buttons.c:9-11](../App/Src/buttons.c#L9-L11)).

- [ ] **5. Tune `PLC_LABEL_DWELL_MS`.** The 1500&nbsp;ms default
  ([plc_config.h:75](../App/Inc/plc_config.h#L75)) is a guess about how long
  a person needs a label glyph held before the next one replaces it — a
  guess about a person, not a number derived from the code. Once someone can
  actually read the cell, expect to revisit it: it is a one-line change if
  the real pace turns out to be too fast or too slow.

---

**Previous:** [10-reader-ui-and-buzzer.md](10-reader-ui-and-buzzer.md) ·
**Index:** [README.md](README.md)
