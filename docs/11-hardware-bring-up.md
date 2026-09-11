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

  **Two faults found here in practice, both worth knowing before you start.**

  *MOSI and MISO swapped.* This presents as `CHIP_FAULT` with the RJ45 link
  LEDs lit and the board otherwise healthy — discs homing, buttons and buzzer
  working. The lit LEDs are the trap: the W5500's PHY negotiates with the
  switch as soon as the module has power, entirely without the MCU, so they
  confirm power and cable and say nothing about SPI. `getVERSIONR()` never
  returns `0x04`, the chip never receives an address, and the device is
  completely invisible on the network — no ARP, no ICMP. Check the two data
  lines against each other before reaching for a multimeter; the module is a
  slave, so its MOSI goes to the MCU's MOSI (PB15) and its MISO to PB14.

  *Everything working and the link still resetting.* If the SPI is proven but
  the connection tears down once per poll, that is a different fault entirely
  and it is documented in
  [09 §9.9b](09-modbus-tcp-and-plc-link.md#99b-a-healthy-socket-that-refuses-to-read),
  along with the SWD technique that found it.

- [ ] **1.5. Read what the board is already telling you, before touching a PC.**
  This step needs no host, no cable to a laptop and no commands. Skipping it is
  how a bench session ends up debugging a network that was never the problem:
  a board that is not running this firmware looks, from the network side,
  exactly like a board with a wiring fault — silent in both cases.

  **The discs home at power-up.** `App_init()` runs `initialize_motors()` and
  then `calibrate_zero_position()` before anything Ethernet exists
  ([app_main.c:84-87](../App/Src/app_main.c#L84-L87)), so both motors visibly
  move within the first seconds. If they do not, the MCU is not executing this
  firmware and nothing further in this guide will show anything. Reflash and
  start again.

  **Learn the three blink codes apart.** They are counts, not patterns, and a
  single glance easily confuses them
  ([fault_led.h:30-32](../App/Inc/fault_led.h#L30-L32)):

  | Blinks | Meaning |
  |---|---|
  | 1 | Motor 01 driver or its ZERO sensor |
  | 2 | Motor 02 driver or its ZERO sensor |
  | 3 | W5500 absent or not answering over SPI |

  Only code 3 is about the network. Codes 1 and 2 mean homing failed, which
  does not stop startup — the reader still runs.

  **With the cable in and no PLC answering, the buzzer should speak about every
  ten seconds.** This is the most informative single observation in the whole
  step, and it is audible from across the room. Once the PHY reports link, the
  link state machine opens a socket and calls `connect()`
  ([plc_link.c:255-278](../App/Src/plc_link.c#L255-L278)). With nothing at
  `10.0.0.204` the connection never completes, so after
  `PLC_RESPONSE_TIMEOUT_MS` (1 s) it backs off, waits `PLC_RECONNECT_DELAY_MS`
  (2 s) and tries again — roughly a three-second cycle, each turn reporting a
  fault. The reader rate-limits that to one sound per
  `PLC_FAULT_SOUND_MIN_INTERVAL_MS`
  ([plc_config.h:83-97](../App/Inc/plc_config.h#L83-L97)), so what you hear is
  the low double `LINK_FAULT` tone roughly every ten seconds.

  **Hearing it proves `plc_link` is running.** Hearing nothing at all — no
  tone, no blink code 3 — is the signature of a board that never reached
  `plc_link_init()`, most often because the flashed image predates the
  integration commit that first wired the link into `App_run()`. Reflash from
  the current tree before looking at the network.

  **The RJ45 link LEDs prove less than they appear to.** The W5500's PHY
  negotiates with the switch as soon as the module has power, entirely without
  the MCU. Lit LEDs therefore confirm power and a live cable — they say nothing
  about whether the firmware ever configured the chip.

- [ ] **1.6. Find the board from a Linux host.** Only worth doing once step 1.5
  says the board is alive and trying. Two facts decide which commands are
  meaningful.

  **Nothing listens.** The firmware is a MODBUS *client*: it opens one socket
  and connects outward ([plc_link.c:261-269](../App/Src/plc_link.c#L261-L269));
  `listen()` appears nowhere in the application. A port scan finds no open port,
  and that is the correct result rather than evidence of a dead board.

  **It answers ping.** The W5500's ping-block bit `MR_PB`
  ([w5500.h:720](../Lib/w5500/ioLibrary/Ethernet/W5500/w5500.h#L720)) is never
  written by any code here, so the chip replies with its power-on default. ICMP
  is the only host-discovery method this firmware offers.

  First, put the host on the board's subnet. This is a secondary address; it
  does not disturb the interface's existing one or the default route:

  ```bash
  IF=eno1                                  # your wired interface, from: ip -br addr
  sudo ip addr add 10.0.0.9/24 dev "$IF"
  ```

  `Error: ipv4: Address already assigned` means it is already there from an
  earlier attempt. That is confirmation, not a failure to fix.

  Then watch for ARP, and **leave it running for at least fifteen seconds** —
  the board only speaks every three:

  ```bash
  sudo tcpdump -i "$IF" -n arp
  ```

  This is the primary test, and it is more informative than a ping. With no PLC
  present the board cannot send its SYN — the W5500 has no destination MAC to
  send it to — so it sits asking for one. Seeing

  ```
  ARP, Request who-has 10.0.0.204 tell 10.0.0.50
  ```

  proves in a single line that the chip is alive over SPI, holds its configured
  address, has PHY link, and is running the connection state machine. Ping
  proves considerably less.

  `ping -c 4 10.0.0.50` and `sudo arping -I "$IF" -c 3 10.0.0.50` are useful
  confirmation afterwards. In `arping` output the MAC should be
  `00:08:dc:11:22:33`; the `00:08:DC` prefix is WIZnet's OUI, which is what
  distinguishes this board from anything else that might answer at that address.
  The rest of its identity is in [plc_config.h](../App/Inc/plc_config.h): IP
  `10.0.0.50/24`, source port `50000`, and the PLC it seeks at
  `10.0.0.204:503` — port 503, not the MODBUS default of 502.

  | What you see | What it means | Where to go |
  |---|---|---|
  | `who-has 10.0.0.204` every ~3 s | Everything works; only the PLC is missing | Step 1.7 |
  | No ARP from the board, no fault tone, no blink code | The board is not running the link at all — most likely a stale flashed image | Back to step 1.5; reflash |
  | No ARP, but blink code 3 repeating | `VERSIONR` failed; the chip never received an address | Step 1, and [09 §9.9](09-modbus-tcp-and-plc-link.md#99-troubleshooting-proving-the-spi-link) |
  | No ARP, no blink code 3, but the fault tone does sound | The link is running and failing before it transmits. Check the switch actually bridges both ports | Capture on the switch's other port |
  | ARP appears but ping does not answer | Contradicts the code — `MR_PB` is never set | Capture it and report it |
  | Only traffic from your router in the capture | The board is not on this segment at all | Confirm both cables reach the same switch |
  | A port scan finds nothing open | Correct — nothing listens | Use ARP and ping |

  Clean up with `sudo ip addr del 10.0.0.9/24 dev "$IF"`.

- [ ] **1.7. Stand in for the PLC.** [tools/plc_sim.py](../tools/plc_sim.py)
  answers the device's polls so the whole reader can be exercised at a desk with
  no PLC in the room. Standard library only — nothing to install.

  There are exactly two commands, in this order:

  ```bash
  python3 tools/verify_plc_sim.py            # 1. is the simulator itself correct?

  sudo ip addr add 10.0.0.204/24 dev "$IF"   # 2. become the PLC
  sudo python3 tools/plc_sim.py --toggle
  ```

  The first needs no privileges, no network and no board: it starts the
  simulator on a loopback port *by itself*, drives it with frames built exactly
  as [modbus_tcp.c](../App/Src/modbus_tcp.c) builds them, and checks the replies
  in the same order `modbus_parse_read_coils()` checks them. **You never pass
  `--bind` or `--port` by hand** — the defaults already match what the firmware
  looks for, and the self-test supplies its own.

  Run it first because the simulator is the thing judging the device. The
  firmware's parser rejects a malformed reply rather than accepting it quietly,
  so a bug in the simulator arrives at the bench wearing the firmware's clothes.

  Port 503 is privileged, hence `sudo` on the second command. With `--toggle`
  the simulator flips one coil in rotation once a minute, which is what
  exercises the queued-package chirp and the automatic jump to newer data when
  NEXT is pressed past the last field.

  A minute is chosen against the reader rather than picked arbitrarily. Walking
  the whole package takes roughly 45 s at the default `PLC_LABEL_DWELL_MS`, so
  one change per minute lands about one new package per complete read — enough
  to exercise the pending slot without a reader who never reaches the end of a
  stable package. `--toggle 5` provokes it far faster when that is the thing
  being tested, at the cost of no longer resembling a production line.

  `--coils` sets an explicit starting package, and every poll is logged with a
  timestamp and the field labels, so what went out on the wire can be compared
  against what the cell renders and against how far apart the events were.

  Success looks like this, on both sides at once: the simulator logs a
  connection from `10.0.0.50` and then a poll every 500 ms, and the board plays
  the `DATA_RECEIVED` tone as the first package lands. From here step 4's
  end-to-end list applies in full.

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
    ([reader_ui.c:185-194](../App/Src/reader_ui.c#L185-L194)).
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

## Known backlog

None of these blocks bring-up. They were raised during review, judged not worth
fixing at the time, and are recorded here so the reasoning is not lost.

**`plc_link`'s `poll_requested` is not cleared when the reader gives up first.**
`reader_ui` runs its own `AWAITING_POLL` deadline, which is what rescues a reader
when `plc_link` is still stuck in `LINK_WAIT`, `CONNECTING` or `CHIP_FAULT` and
has not sent anything yet. If that deadline fires, `plc_link` still has the
explicit request flagged, so the eventual reply arrives as an ordinary background
snapshot — a queued-package chirp, or silence, where a data-received tone would
have fitted better. No data is lost and nothing desyncs. Fixing it would mean one
module reaching into the other's state on a path neither currently owns, for a
symptom that only shows on an already-failing link.

**`tests/test_braille_dots.c` never feeds `translate_char_on_disc()` a three- or
four-byte UTF-8 sequence.** The documented rule that a codepoint above U+00FF
renders blank is therefore covered by reading the code, not by an assertion. The
decoder itself predates this work and was not modified.

**`buzzer.c` documents one of its two preload assumptions.** The comment explains
why a new auto-reload takes effect immediately (`ARPE` is disabled) but says
nothing about the compare-preload assumption the same design rests on. Separately,
a period of 1 would make `CCR == ARR` and stick the output; unreachable at the six
frequencies in use, worth a guard only if the range is ever widened.

**`docs/07-character-encoding.md` has line anchors one line early**, pointing at a
closing `*/` rather than the code they describe. Pre-existing drift of the kind
that has bitten this repo before.

---

**Previous:** [10-reader-ui-and-buzzer.md](10-reader-ui-and-buzzer.md) ·
**Index:** [README.md](README.md)
