# Known limitations

Behaviour and structure that are known and accepted for now, with the reason
and what would change them. None of these blocks normal operation. New items
belong here until they are fixed or moved to the issue tracker.

## Robustness

| Limitation | Effect | What would change it |
|---|---|---|
| No watchdog (IWDG) | A main-loop stall (for example a hung SPI transfer repeated forever) is not recovered | Enable the IWDG and refresh it in `App_run()`; budget for the longest blocking call and for homing in `App_init()` |
| `Error_Handler()` and the fault handlers are silent infinite loops | A clock or peripheral init failure, or a HardFault, leaves the device dark with no blink code | Blink a distinct code from `Error_Handler()` (inside its USER CODE block) and from `HardFault_Handler()` |
| W5500 TCP keep-alive (`Sn_KPALVTR`) and retry timing (`RTR`, `RCR`) are left at chip defaults | A cable pulled while `IDLE` is noticed only when the next request fails or times out | Configure keep-alive after connect |
| `CHIP_FAULT` retries are silent after the first report | A W5500 that disappears after start-up is reported once, then only by the absence of data | Report a fault on every failed retry (the reader rate-limits the sound) |
| `plc_link`'s request flag is not cleared when the reader gives up first | If the reader's own `AWAITING_POLL` timeout fires while the link is still connecting, the eventual reply arrives as a background snapshot (chirp or silence) instead of a confirm tone. No data is lost. | Let the reader cancel the request, which needs a new entry point in the link contract |
| A coil pulse shorter than the polling period can be missed | The device samples coils; it does not receive events | Poll faster, or have the PLC latch short events |
| Homing failure leaves that disc unreferenced | Characters on that disc are rendered relative to its power-up position after a blink code | Refuse to render on an unhomed disc, or retry homing |

## Structure

| Item | Note |
|---|---|
| `motion_planner.c` has three jobs | TMC2209 bring-up, angle → microstep moves and homing share one file. Splitting driver bring-up out is the natural first cut if it grows. |
| Per-motor duplication | Four static structs per motor and two parallel calls in `initialize_motors()`, `calibrate_zero_position()` and `HAL_TIM_PWM_PulseFinishedCallback()`. Readable with two motors; collapse into an array of axes if a third is added. |
| Unused stepper API | `Stepper_Enable()`, `Stepper_Disable()`, `Stepper_IsEnabled()` and `Stepper_Stop()` have no caller: ENN belongs to the TMC2209 instance and no move is aborted. Kept so the stepper module works with a driver whose enable pin is on the stepper side. |
| `plc_link.c` fuses transport and protocol | W5500 bring-up, the TCP connection, Modbus polling and `plc_config.h` addressing are in one module. See [11-adding-a-protocol.md](11-adding-a-protocol.md#114-two-cases) for the proposed split. |
| `Lib/w5500` port depends on `App/` and `Core/` | `w5500_stm32.c` includes `plc_config.h` and `main.h`. Passing the pins and timings into `w5500_stm32_init()` would make the port self-contained. |
| `tmc2209_stm32.c` includes `stm32f103xb.h` directly | In addition to the `TMC2209_STM32_DEVICE_HEADER` mechanism; a port to another family must change both. |
| `Lib/w5500/UPSTREAM.md` refers to a "brief" and a task report | Remnants of the implementation process; the technical content (the local `recv()` change) is accurate. |

## Tests and tooling

| Item | Note |
|---|---|
| No host tests for `stepper.c`, `stepper_hal.c`, `motion_planner.c` (homing included), `plc_link.c`, `buzzer.c` | Verified on hardware only. `stepper.c` and the link state machine are good candidates: both reach hardware only through function pointers or a small set of library calls. |
| UTF-8 decoder coverage | `tests/test_braille_dots.c` feeds one ASCII byte and one two-byte sequence. Three- and four-byte sequences, stray continuation bytes and the overlong/surrogate/range rejections are covered by code reading only. |
| Buzzer timer edge case | A tone whose period is 1 tick would make `CCR == ARR`. Unreachable at the five frequencies in use with a 1 MHz tick; worth a guard if the frequency range is widened. |
| `.vscode/launch.json` | The Cortex-Debug configuration points at `./bin/executable.elf`, which the build does not produce. |
| Doxygen warnings | `README.md` is a Doxygen input, and its relative links into `docs/` are reported as unresolved references because `docs/*.md` is not an input. |
| Debug build size | The Debug MODBUS image uses about 70 % of the 64 KB flash ([02-mcu-resources.md](02-mcu-resources.md#25-memory)). |
