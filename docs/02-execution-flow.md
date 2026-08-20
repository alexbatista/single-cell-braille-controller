# 2. Execution flow

A trace of everything that happens from power-on to a braille cell being
rendered, in the order it actually executes. Read this next to
`App/Src/app_main.c`, `App/Src/motion_planner.c` and `Core/Src/main.c` open
side by side.

## 2.1 Boot, before `main()`

1. `startup_stm32f103xb.s` (assembly, generated) runs first on reset: sets
   the stack pointer, copies `.data` from flash to RAM, zeroes `.bss`, then
   calls `SystemInit()` and jumps to `main()`.
2. `SystemInit()` (`Core/Src/system_stm32f1xx.c`) does minimal early setup;
   the real clock configuration happens explicitly in `main()` a moment
   later, not here.

You will basically never need to touch this — it's standard ARM/CubeMX
boilerplate.

## 2.2 `main()` — `Core/Src/main.c`

```
main()
 ├─ HAL_Init()                 SysTick @ 1 ms, NVIC priority grouping, flash prefetch
 ├─ SystemClock_Config()       HSE (8 MHz) → PLL ×6 → SYSCLK = 48 MHz,
 │                             APB1 = 24 MHz, APB2 = 48 MHz, USB clock = PLL
 ├─ MX_GPIO_Init()             clocks for GPIOA/B/C/D; pin modes + initial levels
 ├─ MX_USART2_UART_Init()      USART2 @ 115200 8N1 → motor 1 (PA2 TX / PA3 RX)
 ├─ MX_USART3_UART_Init()      USART3 @ 115200 8N1 → motor 2 (PB10 TX / PB11 RX)
 ├─ MX_TIM2_Init()             STEP timer motor 1: PSC 47, PWM1 on CH2 (PA1)
 ├─ MX_TIM3_Init()             STEP timer motor 2: PSC 47, PWM1 on CH4 (PB1)
 ├─ MX_USB_DEVICE_Init()       USB CDC stack up and enumerating (see 06)
 ├─ App_init(&huart2, &huart3, &htim2, &htim3)  ── one-time setup, see 2.3
 └─ while (1) { App_run(); }   ── forever, see 2.4
```

Two numbers from this block matter later:

- **`SYSCLK = 48 MHz`** is not a performance choice, it's a USB requirement:
  the F1 USB peripheral needs exactly 48 MHz, which is why the PLL multiplier
  is ×6 from an 8 MHz HSE. It is also what
  `tmc2209_stm32_delay_microseconds()` uses to convert microseconds into DWT
  cycle counts.
- **The STEP timers tick at 1 MHz.** APB1 runs at 24 MHz, and because its
  prescaler is not 1 the timer clock is doubled to 48 MHz (RM0008 clock
  tree); `PSC = 47` then divides that by 48. `stepper_hal.c` derives this at
  runtime rather than hardcoding it (`timer_tick_hz()`), so retuning the clock
  tree in CubeMX does not silently change every step rate.

`MX_USB_DEVICE_Init()` running *before* `App_init()` is deliberate: the host
can enumerate the device and start filling the receive ring while the discs
are still homing. Nothing consumes those bytes until the first `App_run()`.

Everything before `App_init()` is Core/ handing you two ready-to-use
`UART_HandleTypeDef*`, two ready-to-use `TIM_HandleTypeDef*`, a live USB
device, and correctly configured GPIO pins. Everything from `App_init()`
onward is your code.

## 2.3 `App_init()` — one-time setup

`app_main.c` keeps this to two calls:

```c
void App_init(UART_HandleTypeDef *huart_01, UART_HandleTypeDef *huart_02,
              TIM_HandleTypeDef *htim_01, TIM_HandleTypeDef *htim_02) {
  initialize_motors(huart_01, huart_02, htim_01, htim_02);
  (void)calibrate_zero_position();
}
```

### 2.3.1 `initialize_motors()` — `motion_planner.c`

```mermaid
sequenceDiagram
    participant App as app_main.c
    participant MP as motion_planner.c
    participant TMCport as tmc2209_stm32.c
    participant TMCcore as tmc2209.c
    participant Step as stepper.c / stepper_hal.c

    App->>MP: initialize_motors(huart2, huart3, htim2, htim3)
    MP->>MP: force both ENN pins HIGH (disable both drivers)
    MP->>MP: motor_driver_init(motor 1, USART2, ENN=PA5)
    MP->>TMCport: tmc2209_stm32_hal_init(...)
    Note right of TMCport: binds serial_write/read/available/flush<br/>to polled USART2 register access;<br/>binds delay_us/ms to DWT cycle counter
    MP->>MP: HAL_Delay(200 ms) — driver power-up settling
    MP->>TMCcore: tmc2209_setup(tmc, hal, address=0)
    MP->>TMCcore: enable_automatic_current_scaling()<br/>enable_automatic_gradient_adaptation()
    MP->>TMCcore: set_microsteps_per_step(8)<br/>set_run_current(25%)<br/>set_hold_current(25%)
    MP->>TMCcore: tmc2209_enable()
    MP->>TMCcore: is_setup_and_communicating()?
    TMCcore-->>MP: true/false
    alt false
        MP->>MP: blink_warning(1) — blink LED, keep going
    end
    MP->>MP: (repeat motor_driver_init for motor 2, USART3, ENN=PB0)
    MP->>Step: motor_stepper_init(motor 1: TIM2_CH2, DIR=PA4, invert_dir=true)
    Step->>Step: Stepper_Hal_Stm32_Init(...) — bind timer + BSRR writes
    Step->>Step: Stepper_Init(...) — DIR forward, pulse train stopped, disabled
    MP->>Step: (repeat for motor 2: TIM3_CH4, DIR=PA7, invert_dir=true)
    MP->>TMCcore: move_using_step_dir_interface() ×2
    Note right of TMCcore: VACTUAL=0 — driver now obeys<br/>external STEP/DIR pins
```

Step by step, in prose:

1. **Both TMC2209 `ENN` pins are forced high (disabled) immediately.**
   `MX_GPIO_Init()` already boots them high, so this is a belt-and-braces
   line: it keeps `App_init()` safe if the CubeMX defaults ever regress, since
   a driver left enabled with default current settings can heat up before
   you've had a chance to configure it. Each motor is re-enabled at the end of
   its own configuration.
2. **`motor_driver_init()` runs once per motor** (motor 1 on USART2/PA5,
   motor 2 on USART3/PB0). Each call:
   - `tmc2209_stm32_hal_init()` — wires up the STM32 port: from here on,
     any `tmc2209_*` call for this motor reads/writes over that motor's
     UART by polling registers directly (see
     [04-tmc2209-howto.md](04-tmc2209-howto.md)).
   - `HAL_Delay(APP_DRIVER_POWER_UP_MS)` — 200 ms of settling after the UART
     is bound, before the first register write.
   - `tmc2209_setup()` — puts the chip into UART/PDN_UART operating mode
     and writes safe default register values.
   - `tmc2209_enable_automatic_current_scaling()` +
     `enable_automatic_gradient_adaptation()` — **required** here: without
     this, stealthChop (the chopper mode the driver runs in) ignores the
     `IRUN`/`IHOLD` current settings entirely and just applies a fixed weak
     PWM amplitude (~14% of supply), so the motor would be very weak. This is
     called out in a comment in `motion_planner.c` because it's a genuinely
     non-obvious TMC2209 behavior, not a stylistic choice.
   - `set_microsteps_per_step(8)`, `set_run_current(25%)`,
     `set_hold_current(25%)` — the current configuration. 8 microsteps ×
     200 full steps is the `APP_MICROSTEPS_PER_REV = 1600` that every angle
     calculation is built on.
   - `tmc2209_enable()` — clears the software-disable flag and drives `ENN`
     low through the HAL.
   - `tmc2209_is_setup_and_communicating()` — reads registers back over
     UART to confirm the chip actually answered. If this returns `false`,
     `blink_warning()` blinks the onboard LED (1 short blink for motor
     1, 2 for motor 2, repeated 3 times) but **does not halt** — every
     config call above is write-only over a half-duplex UART line, so the
     motor still runs even if the RX half of the link is broken; only
     read-back and diagnostics are lost. The comment in the code names the
     likely fix (check the `PDN_UART` wiring).
3. **`motor_stepper_init()` runs once per motor**, binding the STEP timer
   channel and the DIR pin to a `Stepper_t` instance (`App/Inc/stepper.h`).
   Two details worth noting: the `ENN` pin is *not* passed here — it's already
   owned by that motor's `tmc2209_t` instance from step 2, so the stepper's
   optional enable callback is left `NULL`; and `APP_MOTOR_DIR_INVERTED` is
   passed as `true`, because both discs are geared so that the driver's
   "forward" turns them towards decreasing braille weight. See
   [03-stepper-module.md](03-stepper-module.md).
4. **`tmc2209_move_using_step_dir_interface()`** is called for both motors
   at the end, unconditionally. This sets the internal velocity register
   (`VACTUAL`) to 0, which tells the TMC2209 to stop generating its own
   step pulses and instead follow the external STEP/DIR pins the stepper
   module drives. Without this call the chip could still be in its
   internal-velocity mode from a previous run and would ignore STEP/DIR.

### 2.3.2 `calibrate_zero_position()` — where angle 0 comes from

Position counting starts wherever the discs happened to stop last time, so
before any character can be rendered the firmware has to find a physical
reference. `calibrate_zero_position()` homes each disc against its ZERO
sensor (`ZERO_MOTOR01` on PA9, `ZERO_MOTOR02` on PA8) and declares that point
position 0.

This is the last blocking thing in startup, and it takes several seconds per
disc — the design goal is "never spin forever inside `App_init()`", not "boot
instantly" ([08](08-motion-and-homing.md) §8.7 has the arithmetic). A disc whose sensor never changes state is *not* fatal: the
search is bounded, the motor's blink code is flashed, and startup continues so
the USB port stays available for diagnosing the sensor — which is exactly why
`app_main.c` can afford to ignore the return value with `(void)`. The full
routine, its bounds and its rate limits are covered in
[08-motion-and-homing.md](08-motion-and-homing.md).

## 2.4 `App_run()` — the forever loop

Called back-to-back, forever, from `main()`'s `while(1)`. Each call handles at
most one received byte and returns:

```
App_run()
 ├─ CDC_ReadChar(&c)                    → nothing pending? return immediately
 ├─ HAL_GPIO_TogglePin(LED)             → visible proof a byte arrived
 ├─ CDC_Transmit_FS(&c, 1)              → echo it back to the host terminal
 └─ translate_char_on_disc(c)           → braille_disc.c
     ├─ utf8_decode_byte(c)             → codepoint, or "not yet / invalid"
     ├─ pattern_for_char()              → 6-dot bitmask from the table
     ├─ weight + angle per disc         → disk_angles_t {single_row, double_row}
     └─ move_to_angle(cell)             → motion_planner.c, blocks until arrival
         ├─ steps_to_angle() ×2         → shortest signed microstep delta
         ├─ start_move() ×2             → DIR + Stepper_MoveSteps(1600 Hz)
         └─ wait_until_idle()           → spins while either stepper is busy
```

Three consequences fall out of that shape:

- **One byte per call, one move per character.** A multi-byte UTF-8 sequence
  (every accented letter arrives as two bytes) consumes two `App_run()` calls
  but produces exactly one disc move: the decoder returns a "nothing yet"
  sentinel for the lead byte, and `translate_char_on_disc()` returns without
  touching the motors. See [07-character-encoding.md](07-character-encoding.md).
- **The move is blocking, the reception is not.** `move_to_angle()` spins in
  `wait_until_idle()` until both discs arrive — at most half a revolution at
  1600 microsteps/s, so on the order of half a second. During that time no
  byte is consumed, but the USB interrupt keeps filling the ring buffer behind
  your back. That asymmetry is the whole reason the ring exists
  ([06-usb-cdc.md](06-usb-cdc.md)).
- **Nothing is polled to make the motors turn.** The busy-wait is waiting on
  interrupts, not driving anything: the pulses come out of the timer hardware.
  That's the next section.

## 2.5 One microstep, all the way down

A move is not a loop that toggles a pin. `Stepper_MoveSteps()` sets a step
budget and starts a hardware pulse train; each completed pulse raises an
interrupt that decrements the budget, and the last one stops the train:

```
motion_planner.c: start_move(&stepper_motor_01, delta, 1600)
  → Stepper_SetDirection(stepper, delta > 0)
      → stepper.c: hal.dir_write(ctx, forward)
        → stepper_hal.c: dir_write() — applies invert_dir, then
          GPIOA->BSRR = DIR_MOTOR01_Pin (or << 16)     // one register store
  → Stepper_MoveSteps(stepper, |delta|, 1600)
      → stepper.c: remaining = |delta|
      → hal.pulse_train_start(ctx, 1600)
        → stepper_hal.c: pulse_train_start()
            ticks = timer_tick_hz(TIM2) / 1600 = 1 MHz / 1600 = 625
            ARR = 624, CCR = 312                        // 50% duty
            HAL_TIM_GenerateEvent(UPDATE)               // load the shadow regs
            TIM2->SR = 0                                // drop phantom flags
            HAL_TIM_PWM_Start_IT(TIM2, CH2)             // pin belongs to TIM2 now

… hardware emits pulses on PA1, one every 625 µs, with no CPU involvement …

TIM2_IRQHandler (Core/Src/stm32f1xx_it.c)
  → HAL_TIM_IRQHandler(&htim2)                          // CC2 compare match
    → HAL_TIM_PWM_PulseFinishedCallback(&htim2)         // motion_planner.c
      → Stepper_OnPulseComplete(&stepper_motor_01)      // stepper.c
          position += forward ? 1 : -1
          if (--remaining == 0) hal.pulse_train_stop()  // HAL_TIM_PWM_Stop_IT
```

Points worth holding onto:

- **The compare match, not the update event, is what counts a pulse.** In PWM
  mode 1 the pin goes high when the counter restarts and low at the compare
  match, so one CCx interrupt per completed pulse is exactly the event you
  want to count — the position is advanced only once a pulse is known to have
  finished.
- **The pin is owned by the timer while a move is running.** Stopping the
  channel hands PA1/PB1 back to the GPIO output register, which is 0 out of
  reset, so STEP idles low as the TMC2209 expects.
- **The position counter is the *only* record of where a disc is.** There is
  no encoder. `Stepper_GetPosition()` returns pulses counted since the last
  `Stepper_SetPosition()` — which is what homing calls to define zero.
- **Everything the abstraction costs is one indirect call per pulse**, plus
  the interrupt the hardware would raise anyway. Worth keeping in mind when
  judging whether the layering is "too much": see
  [05-is-it-overengineered.md](05-is-it-overengineered.md).

## 2.6 What runs in interrupt context

| Interrupt | Priority | Body | Does what |
|---|---|---|---|
| `USB_LP_CAN1_RX0_IRQn` | 0 | `HAL_PCD_IRQHandler` → `CDC_Receive_FS` | Copies received bytes into the CDC ring buffer |
| `TIM2_IRQn` | 0 | `HAL_TIM_IRQHandler` → `Stepper_OnPulseComplete(motor 1)` | Counts one STEP pulse, stops the train on the last |
| `TIM3_IRQn` | 0 | same, motor 2 | " |
| `SysTick` | 0 | `HAL_IncTick` | Feeds `HAL_Delay()` / `HAL_GetTick()` |

Everything else — TMC2209 UART traffic, the UTF-8 decoder, angle math, homing
— runs in the main loop. The UTF-8 decoder in particular keeps state between
calls and is documented as main-loop-only for exactly that reason; the USB ISR
must stay a pure producer.
