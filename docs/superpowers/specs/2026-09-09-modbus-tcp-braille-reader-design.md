# MODBUS TCP braille reader — design

**Date:** 2026-09-09
**Status:** approved, ready for implementation planning
**Target:** STM32F103C8T6 (64 KB flash / 20 KB RAM), W5500 Ethernet over SPI2

## 1. Purpose

Replace the USB-typed-character input with a PLC data source: the device reads a
fixed package of 11 boolean signals from a PLC over MODBUS TCP and renders them,
one key/value pair at a time, on the single braille cell. Three buttons
(previous / next / repeat) drive navigation and a buzzer reports every
communication event, so the whole interface is usable without sight.

The USB CDC path is not deleted. It becomes a non-default build variant.

## 2. Constraints that shaped the design

| Constraint | Consequence |
|---|---|
| Flash is 64 KB. Release (`-Os`) uses 22 068 B (34 %); Debug (`-O0 -g3`) uses 46 532 B (71 %) | Shipping has ample room. Compiling the USB stack out of the MODBUS build is what keeps the **Debug** build comfortably linkable, since USB is over half of it. |
| `move_to_angle()` blocks until both discs arrive (~0.5–1 s) | Everything else must be a cooperative tick; a package read takes tens of seconds, so background polling cannot depend on the read finishing. |
| The braille cell shows one glyph at a time | A key/value pair is a *sequence* of glyphs, which needs a sequencer with dwell timing. |
| MODBUS TCP is master/slave and the PLC is the server | The device is a client. A server cannot push to a client, so "unrequested data" is implemented as background polling that detects change. |
| The reader is blind | Every state transition that matters must be audible. The LED is for a sighted technician at boot only. |

## 3. Decisions

Each of these was chosen over a named alternative; the alternatives are recorded
so a future reader does not re-litigate them.

1. **The device is a pure MODBUS TCP client that polls continuously.**
   `POLL_INTERVAL_MS` defaults to 500 ms. When a poll result differs from the
   package being read, that result becomes a *pending* package and the
   "queued" sound plays. Rejected: also running a MODBUS server on port 502 so
   the PLC could write coils in — true push, but it needs PLC-side
   configuration and roughly doubles the MODBUS code on a part that is already
   71 % full.

2. **One press of NEXT plays a whole key/value item and rests on the value.**
   The label glyph(s) are shown for `LABEL_DWELL_MS`, then the value glyph is
   rendered and left in place until the next button press, so the reader always
   ends with a finger on the answer. Rejected: one press per glyph (muddies
   PREV/REPEAT and forces the reader to track whether they are on a letter or a
   value) and value-only (fastest, but no confirmation of which field it is).

3. **Labels are mixed length: two letters for the four SMEMA signals, one for
   the rest.** The SMEMA signals genuinely need the A/B and in/out distinction;
   the other seven have unambiguous single-letter Portuguese mnemonics. Every
   extra letter is an extra blocking disc move, so uniform two-letter labels
   were rejected as a third of the reading time for no information gain.

4. **A single latest-pending slot, not a queue.** A newer poll overwrites the
   pending package. Reading 11 fields takes roughly 45 s, so a FIFO would put
   the reader progressively further behind the real state of the line — they
   would be reading history while the machine has moved on. Overwriting
   guarantees that stepping past the last field lands on the *current* state.

5. **Static IP in a config header, no DHCP.** Saves ioLibrary's `dhcp.c`
   (~3 KB) and removes a boot path that can stall when no DHCP server answers,
   for a device that lives on one fixed machine network.

6. **Two mutually exclusive input variants selected at compile time.**
   `BRAILLE_INPUT_MODBUS` (default) and `BRAILLE_INPUT_USB_CDC`. The USB variant
   compiles today's behaviour unchanged. The MODBUS variant drops
   `usb_device.c`, `usbd_desc.c`, `usbd_cdc_if.c`, `usbd_conf.c` and the
   `Middlewares/` USB class stack from the link. Nothing is deleted; one CMake
   option switches builds.

   This is primarily about keeping the `-O0` Debug build small and about not
   shipping a dead USB stack — **not** about making the feature fit. Release has
   43 KB free (section 11). The saving is bounded by one detail: the generated
   `USB_LP_CAN1_RX0_IRQHandler` in `Core/Src/stm32f1xx_it.c` calls
   `HAL_PCD_IRQHandler(&hpcd_USB_FS)`, and the vector table keeps that handler
   alive through `--gc-sections`. So the MODBUS variant must supply no-op
   definitions of `MX_USB_DEVICE_Init()` and `hpcd_USB_FS`, and the reachable
   part of `stm32f1xx_hal_pcd.c`/`stm32f1xx_ll_usb.c` stays linked. Editing the
   generated call site is not an option — it lies outside any USER CODE region
   and CubeMX would overwrite it.

7. **`buzzer_play()` blocks, bounded at ≤ 400 ms.** This is a deliberate
   exception to the otherwise-cooperative design. PWM keeps sounding in hardware
   without ticks, so a tone started immediately before a ~1 s blocking disc move
   would sound for the whole move unless something stopped it. Blocking on these
   five short patterns removes that truncation bug and the buzzer tick entirely,
   and 400 ms is imperceptible beside a move that already blocks for a second.
   Buttons are EXTI-latched throughout, so no press is lost.

## 4. Architecture

`App_run()` becomes a cooperative tick called forever from the main loop. Each
call services, in order and without blocking: debounced button events, the
Ethernet/MODBUS link state machine, and the presentation sequencer's dwell
timer. All timing derives from `HAL_GetTick()`. The only blocking calls left are
the disc move (existing, unavoidable) and the bounded buzzer patterns.

### 4.1 Modules

| Module | Single purpose | Depends on | Host-testable |
|---|---|---|---|
| `Lib/w5500/` | Vendored ioLibrary `Ethernet/{wizchip_conf,socket}.c` and `Ethernet/W5500/w5500.c`, plus a `w5500_stm32.c` SPI/CS/reset port | HAL SPI2, PB12, PA10 | no |
| `App/Inc/plc_config.h` | Every tunable number: network identity, PLC endpoint, coil range, all timings | — | header only |
| `App/Src/modbus_tcp.c` | Pure MBAP + PDU codec: build FC01 Read Coils, parse and validate the response | nothing | **yes** |
| `App/Src/plc_packet.c` | The packet contract: coil order, braille labels, snapshot type | nothing | **yes** |
| `App/Src/plc_link.c` | Non-blocking connect / poll / timeout / reconnect state machine | ioLibrary, `modbus_tcp` | no |
| `App/Src/reader_ui.c` | Cursor, pending slot, presentation sequencer; emits render and sound commands | `plc_packet` | **yes** |
| `App/Src/buttons.c` | EXTI callbacks to debounced NEXT / PREV / REPEAT events | HAL GPIO | no |
| `App/Src/buzzer.c` | Bounded tone-pattern player on TIM4_CH1 | HAL TIM | no |
| `App/Src/fault_led.c` | `blink_warning()` and the blink-code constants, moved out of `motion_planner.c` | HAL GPIO | no |

The split that matters is `reader_ui.c`: it holds all of the navigation
behaviour and depends on no hardware. It consumes events plus a tick and returns
"render these dots" / "play this sound", which makes the entire specification in
section 6 verifiable on the host.

### 4.2 Changes to existing code

Two targeted changes, both because a third consumer now exists. Neither is a
rewrite.

- **`braille_disc.c`/`.h`** — the 6-dot bitmask is already the internal single
  source of truth but is not reachable, so there is no way to command "all six
  raised". Expose it: `braille_render_dots(uint8_t dots)` (`0x00` all flat,
  `0x3F` all raised) and `braille_render_char(uint8_t latin1)`.
  `translate_char_on_disc()` becomes a thin UTF-8-stream wrapper over
  `braille_render_char()`, used only by the USB variant. The dot table, the
  weight/angle math and the UTF-8 decoder are untouched.
- **`motion_planner.c`** — `blink_warning()` and the blink-code constants move
  to `fault_led.c`/`.h`; `motion_planner.c` includes it. Ethernet faults take
  code 3 (motor 1 is 1, motor 2 is 2).

## 5. Packet contract

One FC01 Read Coils transaction covers the whole package: unit 1, start address
`PLC_COIL_BASE_ADDRESS` (0), quantity `PLC_COIL_COUNT` (11). This matches the
working Python reference client and the ScanBus test session (Coil 0X,
address 0, quantity 11, ID 1).

A snapshot is a `uint16_t` bitfield — 11 coils fit, static-asserted — so
comparing two snapshots for change detection is a single integer compare. A
static assertion ties `PLC_FIELD_COUNT` to `PLC_COIL_COUNT`, so the table below
and the MODBUS request can never disagree about the package size.

| Index | Coil | PLC field | Braille label | Dots |
|---|---|---|---|---|
| 0 | 0 | SMEMA A IN | `A` `E` | 1 / 1-5 |
| 1 | 1 | SMEMA A OUT | `A` `S` | 1 / 2-3-4 |
| 2 | 2 | SMEMA B IN | `B` `E` | 1-2 / 1-5 |
| 3 | 3 | SMEMA B OUT | `B` `S` | 1-2 / 2-3-4 |
| 4 | 4 | SENSOR PRESENÇA | `P` | 1-2-3-4 |
| 5 | 5 | STOP_LINE | `S` | 2-3-4 |
| 6 | 6 | SEND_TIME | `T` | 2-3-4-5 |
| 7 | 7 | VERDE | `V` | 1-2-3-6 |
| 8 | 8 | VERMELHO | `R` | 1-2-3-5 |
| 9 | 9 | AMARELO | `M` | 1-3-4 |
| 10 | 10 | released | `L` | 1-2-3 |

`E` is *entrada* and `S` is *saída*; `R` is ve**R**melho and `M` is a**M**arelo,
disambiguating the three signal-lamp colours. `L` is *liberado* for `released`.

**Why variable-length labels stay unambiguous.** The value glyph is all-flat
(`0x00`) or all-raised (`0x3F`), and no letter in this table is either. After
feeling `A`, the next glyph announces itself: a letter means the label
continues, flat-or-full means the answer arrived. This property is what makes
mixed-length labels safe, and it is fragile — adding a label letter such as `É`,
which *is* all six dots, would break it silently. A test asserts it.

The reference Python client reads 11 coils; the human-readable example in the
original request lists only 10 (it omits `AMARELO`). The script and the ScanBus
capture agree on 11, so 11 is authoritative.

## 6. Reader behaviour

Four states. `cursor` is `int8_t`, where `-1` means "package adopted, reading
not started".

- **`NO_DATA`** — no package adopted; cell blank
- **`AWAITING_POLL`** — an explicit request is in flight
- **`LABEL`** — showing label glyph *n*; dwell timer armed
- **`RESTING`** — has a package; idle on a value, or at `cursor == -1`

Two shorthands used in the table below. *Present field* means: render label
glyph 0 of `plc_fields[cursor]` and arm the dwell timer. `LAST_FIELD` is
`PLC_FIELD_COUNT - 1`; it never appears as a literal in the code.

| Event | State / guard | Behaviour |
|---|---|---|
| NEXT | `NO_DATA` | request poll, `REQUEST_SENT` → `AWAITING_POLL` |
| NEXT | `cursor < LAST_FIELD` | `cursor++`, present field → `LABEL` (the `-1 → 0` case needs no special handling) |
| NEXT | `cursor == LAST_FIELD`, pending full | adopt pending **and clear the slot**, `cursor = 0`, `DATA_RECEIVED`, present → `LABEL` |
| NEXT | `cursor == LAST_FIELD`, no pending | force out-of-cycle poll, `REQUEST_SENT` → `AWAITING_POLL` |
| NEXT | `AWAITING_POLL` | ignored (a request is already in flight) |
| PREV | `cursor > 0` | `cursor--`, present field → `LABEL` |
| PREV | `cursor <= 0` | `BOUNDARY` blip; **discs do not move** |
| REPEAT | `cursor >= 0` | re-present the current field from glyph 0 → `LABEL` |
| REPEAT | `cursor < 0` | `BOUNDARY` blip |
| snapshot | any, while `AWAITING_POLL` | treated as the answer to the outstanding request: adopt, clear any pending slot, `cursor = 0`, `DATA_RECEIVED`, present → `LABEL` |
| snapshot | `NO_DATA` | adopt, `cursor = -1`, `DATA_RECEIVED` → `RESTING`; cell stays blank, first NEXT shows field 0 |
| snapshot | `RESTING` or `LABEL`, differs from current | fill pending slot, `PACKAGE_QUEUED` (rate-limited) |
| snapshot | `RESTING` or `LABEL`, identical | ignored, silent |
| link fault | any | `LINK_FAULT` (rate-limited); `AWAITING_POLL` returns to its previous state |
| tick | `LABEL`, dwell expired | more label glyphs → render the next and re-arm; otherwise render the value → `RESTING` |
| tick | `AWAITING_POLL`, timeout | take the link-fault path |

Because `POLL_INTERVAL_MS` is far shorter than a package read, the pending slot
is normally already full whenever any bit has changed, so stepping past the last
field usually adopts it immediately. The explicit-request path is what happens
when nothing has changed, and it still plays `REQUEST_SENT` then
`DATA_RECEIVED` so the reader hears that the device asked and was answered.

Dwell is measured from *after* the blocking move returns, so each label glyph
costs one move plus `LABEL_DWELL_MS`. A glyph whose disc angles equal the
current ones costs no move at all, because `move_to_angle()` computes a zero
delta — so consecutive identical values render instantly.

**Button coalescing.** Each EXTI sets a **boolean flag, not a counter**. Three
impatient NEXT taps during one blocking move therefore collapse into a single
advance rather than skipping two fields.

## 7. Sounds

TIM4 runs at a 1 MHz tick (PSC 47 on the 48 MHz APB1 timer clock), so
`ARR = 1000000 / frequency_hz - 1` and `CCR = ARR / 2` for 50 % duty. Each
pattern is a table of `{frequency_hz, duration_ms}` steps, ~120 B of flash in
total. Frequencies are named constants, never literals at the use site.

| Sound | Pattern | Meaning |
|---|---|---|
| `REQUEST_SENT` | 880 Hz 60 ms, gap, 1175 Hz 60 ms | rising — the device asked the PLC |
| `DATA_RECEIVED` | 1568 Hz 150 ms | a single confident confirmation |
| `PACKAGE_QUEUED` | 2093 Hz 40 ms × 3 | chirpy triple — new data is waiting |
| `LINK_FAULT` | 220 Hz 200 ms × 2 | low double |
| `BOUNDARY` | 330 Hz 40 ms | short low blip — nothing there: already at field 0, or nothing to repeat |

The three event sounds are separated by pitch *and* texture (rising pair vs.
single tone vs. triple chirp), so they stay distinguishable through a small
piezo in a noisy factory.

## 8. Link and MODBUS layer

`plc_link.c` is a state machine ticked from `App_run()`:

`RESET` (hold PA10 low for `W5500_RESET_LOW_MS`) → `CHIP_INIT` (release, wait
`W5500_BOOT_MS`, `wizchip_init`, `wizchip_setnetinfo`, verify
`VERSIONR == 0x04`) → `LINK_WAIT` (poll the `PHYCFGR` link bit) → `CONNECTING`
(`socket(0, Sn_MR_TCP, local_port, SF_IO_NONBLOCK)` then `connect()`, polling
`getSn_SR()` for `SOCK_ESTABLISHED`) → `IDLE` → `REQUEST_SENT` → `IDLE`.
`FAULT` closes the socket and retries after `RECONNECT_DELAY_MS`.

Socket 0 receives all 16 KB of the W5500's TX and RX buffer; sockets 1–7 get
zero, since only one connection is ever opened.

SPI uses ioLibrary's burst callbacks over `HAL_SPI_TransmitReceive` rather than
byte-at-a-time callbacks. SPI is touched only from the tick, never from an ISR,
so `wizchip_cris_enter`/`wizchip_cris_exit` are no-ops.

**`ETH_INT` (PB5) is wired and owned, but nothing acts on it.** This is a
decision, not an omission. `plc_link_tick()` runs on every pass of the main
loop, so the socket is already serviced as promptly as an interrupt could ask
for — there is no wait for the pin to short-circuit. The W5500's `SIMR`/`Sn_IMR`
are left masked accordingly, so the line never asserts. `plc_link_on_exti()`
exists so PB5 has a declared owner in the shared `EXTI9_5` handler, and so an
interrupt-driven or low-power revision has somewhere to start. A flag that gated
nothing behind a comment claiming it was an optimisation would be worse than
not having one; if this changes, change this section too.

`modbus_tcp.c` validates and rejects: frames shorter than a legal response, a
non-zero protocol ID, a **stale transaction ID** (the TID increments per request
and wraps), a mismatched unit ID, exception responses (`FC | 0x80`, whose
exception code is surfaced), and byte counts inconsistent with
`(quantity + 7) / 8`.

## 9. Configuration

`App/Inc/plc_config.h` holds every number, so no literal appears at a use site.

| Constant | Default |
|---|---|
| Device IP / mask / gateway | `10.0.0.50` / `255.255.255.0` / `10.0.0.1` |
| Device MAC | `00:08:DC:11:22:33` |
| PLC endpoint | `10.0.0.204:503`, unit 1 |
| `PLC_LOCAL_PORT` | 50000 |
| Coil range | base 0, count 11 |
| `LABEL_DWELL_MS` | 1500 |
| `POLL_INTERVAL_MS` | 500 |
| `RESPONSE_TIMEOUT_MS` | 1000 |
| `RECONNECT_DELAY_MS` | 2000 |
| `BUTTON_DEBOUNCE_LOCKOUT_MS` | 40 |
| `QUEUED_SOUND_MIN_INTERVAL_MS` | 5000 |
| `FAULT_SOUND_MIN_INTERVAL_MS` | 10000 |

`LABEL_DWELL_MS` at 1500 puts a full 11-field read at roughly 45 s. It is a
one-line change if that proves too slow or too fast in use.

## 10. Error handling

| Failure | Reader-visible behaviour |
|---|---|
| W5500 absent or `VERSIONR` wrong | `LINK_FAULT`, LED blink code 3 at boot, retry from `RESET` every `RECONNECT_DELAY_MS` |
| Cable unplugged (`PHYCFGR` link down) | `LINK_FAULT`, rate-limited to `FAULT_SOUND_MIN_INTERVAL_MS`, so a dead cable does not buzz continuously |
| TCP connect refused or dropped | `LINK_FAULT`, socket closed, reconnect after `RECONNECT_DELAY_MS` |
| Response timeout | `LINK_FAULT`; the current package is kept, not discarded |
| MODBUS exception or malformed frame | treated as a fault; the snapshot is not adopted |
| Disc homing failure at boot | unchanged — existing blink code 1 or 2, startup continues |

The current package is never discarded because of a communication failure: a
reader mid-package keeps reading what they have, and only hears that the link
is unhappy.

## 11. Flash budget

Measured on the pre-implementation tree, both presets, with
`-ffunction-sections -fdata-sections -Wl,--gc-sections` already enabled:

| Preset | Flags | Flash | of 64 KB | Free |
|---|---|---|---|---|
| Release | `-Os -g0` | 22 068 B | 33.7 % | 43 468 B |
| Debug | `-O0 -g3` | 46 532 B | 71.0 % | 18 624 B |

RAM is 7 312 B of 20 480 (35.7 %) in Release.

**There is no flash crisis.** The earlier 71 % reading was the Debug build, and
`-O0` roughly doubles this code. ioLibrary's `Ethernet/` tree plus MODBUS and the
new modules will fit either preset with room to spare; the USB gating is about
build hygiene and Debug headroom, not feasibility.

USB's compiled cost, summed over `usb_device.c`, `usbd_desc.c`, `usbd_cdc_if.c`,
`usbd_conf.c`, the four `Middlewares/` class-stack files, `stm32f1xx_hal_pcd.c`,
`stm32f1xx_hal_pcd_ex.c` and `stm32f1xx_ll_usb.c`, is **27 775 B in Debug** and
**10 899 B in Release** — over half the Debug image. These are pre-`--gc-sections`
object sizes, so the realised saving is smaller, and smaller again because the
retained USB ISR keeps part of the PCD/LL layer linked (decision 6).

`arm-none-eabi-size` is recorded after each implementation step rather than once
at the end, so any regression is attributable to a single task. The levers, if it
ever does tighten, in order:

1. Ship from the Release preset — on its own this is a 24 KB difference.
2. Vendor only ioLibrary's `Ethernet/` tree: no `Internet/`, so no DHCP, DNS,
   SNTP, HTTP or FTP.
3. Drop the `ETH_INT` optimisation (section 8), which is pure addition.

## 12. Testing

A committed host harness under `tests/`: a plain `Makefile` driving host `gcc`,
formalising the throwaway HAL-stub technique from earlier sessions into
`tests/stubs/stm32f1xx_hal.h`. Pure modules are compiled by including the `.c`
directly so `static` functions are reachable.

| Suite | Covers |
|---|---|
| `test_modbus_tcp` | build/parse round-trip; TID mismatch; exception response; truncated frame; wrong byte count; bit extraction across all 11 coils |
| `test_reader_ui` | every row of the section 6 table, driven by a fake clock, asserting the emitted (render, sound) command log |
| `test_plc_packet` | 11 unique ascending coils; labels 1–2 characters; every label letter has a non-zero dot pattern; **no label glyph equals `0x00` or `0x3F`** |
| `test_braille_dots` | `0x00` and `0x3F` produce the expected disc angles; existing character patterns unchanged by the refactor |

`test_reader_ui` is the point of the whole decomposition: verifying 11-field
navigation on hardware costs about 45 s per attempt, and the pending-slot and
end-of-package paths are exactly where a subtle mistake would hide.

## 13. Documentation

New: `docs/09-modbus-tcp-and-plc-link.md`, `docs/10-reader-ui-and-buzzer.md`.
Updated: `docs/README.md` (index and pin map), `docs/01-architecture-layers.md`
(the new App modules and `Lib/w5500`), `docs/02-execution-flow.md` (the
cooperative `App_run()` tick replacing the CDC-driven loop),
`docs/06-usb-cdc.md` (now the non-default build variant).

## 14. Out of scope

- Writing coils back to the PLC. The Python reference has a commented-out
  `write_single_coil`; nothing in this design commands the line.
- Reading anything but the fixed 11-coil package — no register reads, no
  runtime-configurable address map.
- Multi-cell or multi-character braille output. The device has one cell.
- Runtime network configuration. `plc_config.h` is edited and reflashed.
