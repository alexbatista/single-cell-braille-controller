# 4. TMC2209 library — how to use it

This is a practical reference for `Lib/tmc2209`: what to call, in what
order, and what each function this project actually uses does. It
deliberately stays out of `tmc2209.c`'s internals (UART datagram framing,
CRC, register bit layouts) — treat the library as a black box for now. The
full API surface (`Lib/tmc2209/tmc2209.h`, 421 lines) is much bigger than
what this project uses; §4.4 maps the rest so you know it's there when you
need it, without having to learn it today.

## 4.1 The mental model

One `tmc2209_t` instance = one physical TMC2209 chip = one motor. You:

1. Give it a HAL (how to talk UART + delay on *this* MCU) via
   `tmc2209_stm32_hal_init()`.
2. Bring it up with `tmc2209_setup()`.
3. Call a handful of `tmc2209_set_*`/`tmc2209_enable_*` functions to
   configure it — these are all one-way UART writes.
4. Optionally call `tmc2209_get_*`/`tmc2209_is_*` functions to read
   something back — these need the RX line actually wired and working.
5. From then on, you don't call the library per step — you drive the
   physical STEP/DIR pins yourself (via the [stepper module](03-stepper-module.md))
   and the chip does the rest in hardware.

This project has **two** independent instances, one per motor
(`tmc_motor_01` on USART2, `tmc_motor_02` on USART3), each with its own
`tmc2209_stm32_t` port struct holding that instance's UART peripheral, ring
buffer, and enable pin.

## 4.2 Setup call sequence used by this project

This is exactly `motor_driver_init()` in `App/Src/motion_planner.c`, in order,
with what each call is *for*:

```c
tmc2209_hal_t hal;
tmc2209_stm32_hal_init(tmc_port, &hal, usart, enable_port, enable_pin);
// ^ Binds `hal`'s function pointers to this project's STM32 CMSIS
//   implementation (see §4.3). Nothing is sent to the chip yet.

HAL_Delay(APP_DRIVER_POWER_UP_MS);   // 200 ms
// ^ Settling time the driver needs after its UART is bound, before the
//   first register write. Skipping it makes the first tmc2209_setup()
//   land on a chip that is not ready to answer.

tmc2209_setup(tmc, &hal, TMC2209_SERIAL_ADDRESS_0);
// ^ Stores the hal in `tmc`, puts the chip into UART operating mode,
//   writes default register values over the wire. This is the first
//   thing that actually talks to hardware.

tmc2209_enable_automatic_current_scaling(tmc);
tmc2209_enable_automatic_gradient_adaptation(tmc);
// ^ Turns on closed-loop current regulation. Without this, in the
//   stealthChop mode this driver runs in, IRUN/IHOLD below are IGNORED
//   and the coils only get a fixed, weak PWM amplitude. Easy to miss —
//   the motor will turn but with very little torque if you skip this.

tmc2209_set_microsteps_per_step(tmc, 8);
tmc2209_set_run_current(tmc, 25);   // percent, 0-100
tmc2209_set_hold_current(tmc, 25);  // percent, 0-100
// ^ The actual configuration: 8 microsteps per full step, 25% current
//   both while moving and while holding. 8 x 200 full steps is the
//   1600 microsteps/revolution every angle calculation is built on
//   (APP_MICROSTEPS_PER_REV), so changing the microstepping here changes
//   the planner's step math with it.

tmc2209_enable(tmc);
// ^ Clears the chip's software-disable flag AND, since the hal provides
//   set_hardware_enable_pin, drives ENN low (driver energized).

bool ok = tmc2209_is_setup_and_communicating(tmc);
// ^ Reads registers back over UART to confirm the chip is alive and
//   configured. Needs RX wired; see the warning in §4.5.
```

Then, once per motor, still in `initialize_motors()`:

```c
tmc2209_move_using_step_dir_interface(tmc);
// ^ Sets the internal velocity register (VACTUAL) to 0, telling the chip
//   to stop generating its own steps and instead follow the external
//   STEP/DIR pins. Without this the chip could still be in internal
//   velocity mode and would ignore the stepper module entirely.
```

After that, this project never calls into `tmc2209.c` again during normal
operation — motion happens entirely through GPIO pulses via
[stepper.h](03-stepper-module.md).

## 4.3 What the STM32 port (`tmc2209_stm32.c`) actually does

`tmc2209_stm32_hal_init()` fills in six function pointers
(`Lib/tmc2209/tmc2209.h:37-64`, the `tmc2209_hal_t` struct):

| HAL callback | STM32 implementation |
|---|---|
| `serial_write` | Loop, writing bytes into the USART's data register, waiting for the TXE ("transmit empty") flag between each — while also draining incoming bytes, because the TMC2209's single-wire UART echoes every transmitted byte back on RX. |
| `serial_available` / `serial_read` | Drain whatever the receiver has into a small software ring buffer (`TMC2209_STM32_RX_BUFFER_SIZE = 16` bytes) and report/pop from that. There's no RX interrupt — this project polls the UART status register instead. |
| `serial_flush` | Busy-wait for the TC ("transmission complete") flag, still draining RX meanwhile. |
| `delay_microseconds` / `delay_milliseconds` | Busy-wait using the Cortex-M3 DWT cycle counter (`DWT->CYCCNT`), calibrated against `SystemCoreClock` (48 MHz here). |
| `set_hardware_enable_pin` | Direct `BSRR` write to the `ENN` GPIO, inverted (ENN is active-low). `NULL` if no enable pin was given. |

Two things worth noticing: this **never calls `HAL_UART_*`** — even though
`Core/Src/usart.c` configured the peripheral with `HAL_UART_Init()`, actual
byte-level traffic bypasses the HAL and pokes `USART2->SR`/`DR` (or
`->ISR`/`RDR`/`TDR` on newer STM32 families — the port has both variants
behind a compile-time compatibility macro) directly. And it's **polled, not
interrupt-driven** — `serial_available()` actively drains the receiver
every time it's called, so as long as the library calls it often enough
while waiting for a reply (which it does, once per expected byte), the
hardware receiver never overruns even without an RX interrupt.

## 4.4 The rest of the API (available, not currently used)

Everything below exists in `tmc2209.h` and works the same way (call it once
after `tmc2209_setup()`), but nothing in this project calls it yet. Grouped
by purpose so you know where to look when you need one:

| Category | Functions | Needs RX? |
|---|---|---|
| Direction/orientation | `enable/disable_inverse_motor_direction` | No |
| Alternate current API | `set_hold_delay`, `set_all_current_values`, `set_rms_current` (compute from mA + sense resistor instead of percent) | No |
| Chopper waveform tuning | `enable/disable_double_edge`, `enable/disable_vsense`, `set_pwm_offset`, `set_pwm_gradient` | No |
| Power saving at rest | `set_standstill_mode` (freewheel / brake), `set_power_down_delay` | No |
| StealthChop↔SpreadCycle | `enable/disable_stealth_chop`, `set_stealth_chop_duration_threshold` (switch to the louder-but-higher-torque mode above a speed) | No |
| Sensorless load feedback | `set_stall_guard_threshold`, `enable/disable_cool_step` + its 3 tuning functions | Practically yes (StallGuard result is read back) |
| Sense resistor mode | `enable/disable_analog_current_scaling`, `use_external/internal_sense_resistors` | No |
| Internal velocity mode | `tmc2209_move_at_velocity(tmc, microsteps_per_period)` — the alternative to STEP/DIR, where the chip generates steps itself | No |
| Communication tuning | `set_reply_delay` | N/A |
| Diagnostics/telemetry | `get_version`, `is_communicating`, `is_communicating_but_not_setup`, `hardware_disabled`, `get_settings`, `get_status` (temperature/short-circuit flags), `get_global_status`, `clear_reset`, `clear_drive_error`, `get_interface_transmission_counter`, `get_interstep_duration`, `get_stall_guard_result`, `get_pwm_scale_sum/auto`, `get_pwm_offset/gradient_auto`, `get_microstep_counter`, `get_microsteps_per_step` | **Yes**, all of them |

If you later want stall detection, that's `set_stall_guard_threshold` +
`enable_cool_step` + periodically reading `get_stall_guard_result` — but
that's a feature to add, not something already lurking unused in this
codebase. Note that homing does *not* need it here: each disc has its own
optical ZERO sensor read as a plain GPIO, so the reference comes from a real
flag rather than from inferred motor load (see
[08-motion-and-homing.md](08-motion-and-homing.md)).

## 4.5 The failure mode you'll actually see on a breadboard

`blink_warning()` in `motion_planner.c` blinks the LED (short blinks = motor
index, repeated 3 times) when `tmc2209_is_setup_and_communicating()` comes
back `false`, but keeps running rather than halting — because every
`tmc2209_set_*`/`tmc2209_enable_*` call above is a one-way UART write, the
motor still receives its configuration and runs even if the chip's replies
never make it back. The comment in the code names the likely cause: the
`PDN_UART` pin is a single wire that both drives and is driven, so it needs
TX connected through a ~1 kΩ resistor and RX tied directly to the same
node — if RX is wired straight to TX instead of to `PDN_UART`, you'll see
exactly this symptom: the motor moves, and the LED flashes that motor's code
three times during startup.

One caveat when reading blink codes: the same `blink_warning()` and the same
per-motor code are also used when that disc fails to home
([08-motion-and-homing.md](08-motion-and-homing.md) §8.8). A board with a
UART problem *and* a healthy sensor blinks once during driver bring-up; a
board with a sensor problem blinks later, after the drivers are up. If you see
the code twice for the same motor, both faults are present.
