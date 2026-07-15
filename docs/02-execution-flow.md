# 2. Execution flow

A trace of everything that happens from power-on to the motors spinning,
in the order it actually executes. Read this next to `App/Src/app_main.c`
and `Core/Src/main.c` open side by side.

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
 ├─ SystemClock_Config()       HSE (8 MHz) → PLL ×4 → SYSCLK = 32 MHz
 ├─ MX_GPIO_Init()             clocks for GPIOA/B/C/D; pin modes + initial levels
 ├─ MX_USART2_UART_Init()      USART2 @ 115200 8N1 → motor 1 (PA2 TX / PA3 RX)
 ├─ MX_USART3_UART_Init()      USART3 @ 115200 8N1 → motor 2 (PB10 TX / PB11 RX)
 ├─ App_init(&huart2, &huart3) ── one-time setup, see 2.3
 └─ while (1) { App_run(); }   ── forever, see 2.4
```

The `SYSCLK = 32 MHz` number matters later: it's what
`tmc2209_stm32_delay_microseconds()` uses to convert microseconds into DWT
cycle counts, and it's why `step_pulse_width_delay()` in `app_main.c` (an
empty 8-iteration loop) is "roughly a microsecond" — comments in the code
call this number out explicitly for that reason.

Everything before `App_init()` is Core/ handing you two ready-to-use
`UART_HandleTypeDef*` (`huart2`, `huart3`) and correctly configured GPIO
pins. Everything from `App_init()` onward is your code.

## 2.3 `App_init()` — one-time setup

```mermaid
sequenceDiagram
    participant main
    participant App as app_main.c
    participant TMCport as tmc2209_stm32.c
    participant TMCcore as tmc2209.c
    participant Step as stepper.c / stepper_hal.c

    main->>App: App_init(&huart2, &huart3)
    App->>App: force both ENN pins HIGH (disable both drivers)
    App->>App: motor_driver_init(motor 1, USART2, ENN=PA5)
    App->>TMCport: tmc2209_stm32_hal_init(...)
    Note right of TMCport: binds serial_write/read/available/flush<br/>to polled USART2 register access;<br/>binds delay_us/ms to DWT cycle counter;<br/>enables DWT
    App->>TMCcore: tmc2209_setup(tmc, hal, address=0)
    Note right of TMCcore: puts chip in UART mode,<br/>writes default registers
    App->>TMCcore: enable_automatic_current_scaling()<br/>enable_automatic_gradient_adaptation()
    App->>TMCcore: set_microsteps_per_step(8)<br/>set_run_current(80%)<br/>set_hold_current(40%)
    App->>TMCcore: tmc2209_enable()
    Note right of TMCcore: clears software-disable flag,<br/>drives ENN low via HAL
    App->>TMCcore: is_setup_and_communicating()?
    TMCcore-->>App: true/false
    alt false
        App->>App: motor_comm_warning(1) — blink LED, keep going
    end
    App->>App: (repeat motor_driver_init for motor 2, USART3, ENN=PB0)
    App->>Step: motor_stepper_init(motor 1: STEP=PA1, DIR=PA4)
    Step->>Step: Stepper_Hal_Stm32_Init(...) — bind BSRR writes
    Step->>Step: Stepper_Init(...) — DIR=high, STEP=low, disabled
    App->>Step: (repeat for motor 2: STEP=PB1, DIR=PA7)
    App->>TMCcore: move_using_step_dir_interface() ×2
    Note right of TMCcore: VACTUAL=0 — driver now obeys<br/>external STEP/DIR pins
```

Step by step, in prose:

1. **Both TMC2209 `ENN` pins are forced high (disabled) immediately.**
   `MX_GPIO_Init()` leaves them low (enabled) with an uncontrolled power-on
   current — a driver left enabled with default current settings can heat
   up before you've had a chance to configure it, so this is a safety line,
   not boilerplate.
2. **`motor_driver_init()` runs once per motor** (motor 1 on USART2/PA5,
   motor 2 on USART3/PB0). Each call:
   - `tmc2209_stm32_hal_init()` — wires up the STM32 port: from here on,
     any `tmc2209_*` call for this motor reads/writes over that motor's
     UART by polling registers directly (see
     [04-tmc2209-howto.md](04-tmc2209-howto.md)).
   - `tmc2209_setup()` — puts the chip into UART/PDN_UART operating mode
     and writes safe default register values.
   - `tmc2209_enable_automatic_current_scaling()` +
     `enable_automatic_gradient_adaptation()` — **required** here: without
     this, stealthChop (the chopper mode the driver runs in) ignores the
     `IRUN`/`IHOLD` current settings entirely and just applies a fixed weak
     PWM amplitude, so the motor would be very weak. This is called out in
     a comment in `app_main.c` because it's a genuinely non-obvious TMC2209
     behavior, not a stylistic choice.
   - `set_microsteps_per_step(8)`, `set_run_current(80%)`,
     `set_hold_current(40%)` — the actual demo configuration.
   - `tmc2209_enable()` — clears the software-disable flag and drives `ENN`
     low through the HAL.
   - `tmc2209_is_setup_and_communicating()` — reads registers back over
     UART to confirm the chip actually answered. If this returns `false`,
     `motor_comm_warning()` blinks the onboard LED (1 short blink for motor
     1, 2 for motor 2, repeated 3 times) but **does not halt** — every
     config call above is write-only over a half-duplex UART line, so the
     motor still runs even if the RX half of the link is broken; only
     read-back and diagnostics are lost. The comment in the code names the
     likely fix (check the `PDN_UART` wiring).
3. **`motor_stepper_init()` runs once per motor**, binding the STEP/DIR
   pins to a `Stepper_t` instance (`App/Inc/stepper.h`). The `ENN` pin is
   *not* passed here — it's already owned by that motor's `tmc2209_t`
   instance from step 2, so the stepper's optional enable callback is left
   `NULL`. See [03-stepper-module.md](03-stepper-module.md).
4. **`tmc2209_move_using_step_dir_interface()`** is called for both motors
   at the end, unconditionally. This sets the internal velocity register
   (`VACTUAL`) to 0, which tells the TMC2209 to stop generating its own
   step pulses and instead follow the external STEP/DIR pins the stepper
   module drives. Without this call the chip could still be in its
   internal-velocity mode from a previous run and would ignore STEP/DIR.

## 2.4 `App_run()` — the forever loop

Called back-to-back, forever, from `main()`'s `while(1)`. Each call performs
exactly one full mechanical revolution of both motors and returns:

```
App_run()
 ├─ toggle LED
 └─ rotate_one_revolution(clockwise = true)
     ├─ Stepper_SetDirection(motor1, true)   → DIR pin high
     ├─ Stepper_SetDirection(motor2, true)   → DIR pin high
     └─ for step in 1..APP_MICROSTEPS_PER_REV (1600):
          due = start_tick + step * APP_ROTATION_TIME_MS / APP_MICROSTEPS_PER_REV
          busy-wait until HAL_GetTick() reaches "due"
          Stepper_StepPulseBegin(motor1)   → STEP pin high
          Stepper_StepPulseBegin(motor2)   → STEP pin high
          step_pulse_width_delay()          → ~1 µs busy loop
          Stepper_StepPulseEnd(motor1)     → STEP pin low, position++
          Stepper_StepPulseEnd(motor2)     → STEP pin low, position++
```

`APP_MICROSTEPS_PER_REV` is `200 (full steps/rev) × 8 (microsteps) = 1600`.
`APP_ROTATION_TIME_MS` is `2500`, so each microstep is paced roughly
`2500/1600 ≈ 1.56 ms` apart — but note it's paced against an absolute
deadline (`start + step * total / count`) computed fresh every iteration,
not by sleeping a fixed increment each time, so timing doesn't drift even
if one iteration takes longer than another. This whole function is
**blocking**: nothing else runs on the MCU while a revolution is in
progress (there's no RTOS, no interrupts driving this loop — it's a bare
superloop). `App_run()` returning is what lets `main()`'s `while(1)` call it
again for the next revolution; from the outside this looks like continuous
rotation with the LED blinking once per revolution.

## 2.5 One microstep, all the way down

To tie the layers together, here's what a single `Stepper_StepPulseBegin` →
`Stepper_StepPulseEnd` pair actually costs at the bottom:

```
Stepper_StepPulseBegin(&stepper_motor_01)
  → stepper.c: hal.step_write(ctx, true)
    → stepper_hal.c: step_write(ctx, true)
      → gpio_write(GPIOA, STEP_MOTOR01_Pin, true)
        → GPIOA->BSRR = STEP_MOTOR01_Pin        // one register store, sets PA1 high

Stepper_StepPulseEnd(&stepper_motor_01)
  → stepper.c: hal.step_write(ctx, false); position += forward ? 1 : -1
    → stepper_hal.c: step_write(ctx, false)
      → GPIOA->BSRR = STEP_MOTOR01_Pin << 16    // one register store, sets PA1 low
```

Every layer in between compiles away to that single `BSRR` store — the
"abstraction" costs one indirect function call per pulse edge, not a
runtime hierarchy. Worth keeping in mind when judging whether the layering
is "too much": see [05-is-it-overengineered.md](05-is-it-overengineered.md).
