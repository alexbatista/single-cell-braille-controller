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

- [ ] **1.5. Prove the device on the network, from a Linux host.** Once the
  SPI link is good the chip has an address, and a host on the same segment can
  confirm it independently of anything the board reports about itself. Two
  things about this device make the obvious approach misleading, so read them
  before running anything.

  **Nothing listens.** The firmware is a MODBUS *client*: it opens one socket
  and connects outward
  ([plc_link.c:261-269](../App/Src/plc_link.c#L261-L269)); `listen()` appears
  nowhere in the application. A port scan finds no open port, and that is the
  correct result, not evidence of a dead board. ICMP is the only host-discovery
  method this firmware supports — the W5500's ping-block bit `MR_PB`
  ([w5500.h:720](../Lib/w5500/ioLibrary/Ethernet/W5500/w5500.h#L720)) is never
  written by any code here, so the chip answers ping with its power-on default.

  **The chip only gets an address if SPI works.** `wizchip_setnetinfo()` runs
  only after `getVERSIONR()` returns `0x04` and `wizchip_init()` succeeds
  ([plc_link.c:106-124](../App/Src/plc_link.c#L106-L124)). A W5500 that does not
  answer over SPI leaves the device with no IP at all: no ARP, no ICMP, nothing
  on the wire. That failure is step 1's, not this step's, and it announces
  itself as blink code 3 ([fault_led.h:32](../App/Inc/fault_led.h#L32)).

  The single most informative observation is an ARP request. With no PLC
  present, the device cannot send its SYN — the W5500 has no destination MAC to
  send it to — so it sits repeating `who-has 10.0.0.204`. Seeing that one line
  proves the chip is alive over SPI, has its address configured, has PHY link,
  and is running the connection state machine. It settles the question in a way
  a ping does not.

  ```bash
  IF=eno1                                   # the wired interface facing the board
  sudo ip addr add 10.0.0.9/24 dev "$IF"    # secondary address, non-destructive

  sudo arp-scan --interface="$IF" --localnet | grep -i '00:08:dc'
  # or, with nothing extra installed:
  sudo arping -I "$IF" -c 3 10.0.0.50

  ping -c 4 10.0.0.50
  sudo tcpdump -i "$IF" -n -e host 10.0.0.50 or arp
  ```

  `00:08:DC` is WIZnet's OUI, which is what distinguishes this board from
  anything else that might answer at that address. The device's own identity is
  in [plc_config.h](../App/Inc/plc_config.h): MAC `00:08:DC:11:22:33`, IP
  `10.0.0.50/24`, source port `50000`, and the PLC it seeks at `10.0.0.204:503`
  — port 503, not the MODBUS default of 502.

  | Symptom | Reading |
  |---|---|
  | Blink code 3, nothing on the wire | `VERSIONR` failed; the chip never got an address. Back to step 1. |
  | No blink code 3, still no ARP from the board | PHY has no link. The firmware emits nothing at all while waiting for it. Check the module's link LEDs and the cable. |
  | ARP from the board, but ping does not answer | Contradicts the code — `MR_PB` is never set. Capture it and report it. |
  | `who-has 10.0.0.204` repeating | Everything works; only the PLC is missing. Go to the simulator below. |
  | SYN to `10.0.0.204:503` unanswered | Whatever should be answering is not listening, or is on 502. |
  | A port scan finds nothing open | Correct. Use ARP and ping. |

  Clean up afterwards with `sudo ip addr del 10.0.0.9/24 dev "$IF"`.

- [ ] **1.6. Stand in for the PLC.** [tools/plc_sim.py](../tools/plc_sim.py) is
  a MODBUS TCP server that answers the device's polls, so the whole reader can
  be exercised at a desk with no PLC in the room. It uses only the Python
  standard library.

  ```bash
  sudo ip addr add 10.0.0.204/24 dev "$IF"
  sudo python3 tools/plc_sim.py --toggle 7
  ```

  Port 503 is privileged, hence `sudo`. With `--toggle` it flips one coil in
  rotation, which is what exercises the queued-package chirp and the automatic
  jump to newer data when NEXT is pressed past the last field. `--coils` sets an
  explicit starting package, and every poll is logged with the field labels so
  what went out on the wire can be compared against what the cell renders.

  The simulator is the thing judging the device, so judge it first:
  `python3 tools/verify_plc_sim.py` drives it with frames built exactly as
  [modbus_tcp.c](../App/Src/modbus_tcp.c) builds them and checks the replies in
  the same order `modbus_parse_read_coils()` checks them. It needs no
  privileges and nothing installed. This matters because the firmware's parser
  rejects a malformed reply rather than accepting it quietly — so a bug in the
  simulator arrives at the bench wearing the firmware's clothes.

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
