# 2. Execution flow

A trace of everything that happens from power-on to the device being usable,
in the order it actually executes. This describes the default MODBUS build
(`BRAILLE_INPUT=MODBUS`); where the USB_CDC variant differs, it's called out
inline and covered in full in [06-usb-cdc.md](06-usb-cdc.md). Read this next
to `App/Src/app_main.c`, `App/Src/motion_planner.c`, `App/Src/plc_link.c` and
`Core/Src/main.c` open side by side.

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
 ├─ MX_USB_DEVICE_Init()       MODBUS build: usb_device_stub.c's no-op (06);
 │                             USB_CDC build: real stack, enumerating (see 06)
 ├─ MX_SPI2_Init()             SPI2 @ 12 Mbit/s, mode 0 MSB-first → W5500
 ├─ MX_TIM4_Init()             PSC 47 → 1 MHz tick, PWM ready → buzzer
 ├─ App_init(&huart2, &huart3, &htim2, &htim3, &htim4, &hspi2)  ── see 2.3
 └─ while (1) { App_run(); }   ── forever, see 2.4
```

Two numbers from this block matter later:

- **`SYSCLK = 48 MHz`** was originally a USB requirement — the F1 USB
  peripheral needs exactly 48 MHz — and the clock tree still targets it even
  though the default build never enumerates USB. `Core/Src/main.c` is
  CubeMX-generated from the `.ioc` file and is not conditioned on
  `BRAILLE_INPUT`, so removing the USB peripheral from the clock tree would
  mean removing it from the `.ioc` — a bigger change than switching build
  variants, and one that would force every other timing derived from this
  tree (SPI2's prescaler, both USARTs' baud dividers,
  `tmc2209_stm32_delay_microseconds()`) to be re-derived along with it. See
  [06-usb-cdc.md](06-usb-cdc.md) for the rest of what stays linked for the
  same reason.
- **The STEP timers tick at 1 MHz.** APB1 runs at 24 MHz, and because its
  prescaler is not 1 the timer clock is doubled to 48 MHz (RM0008 clock
  tree); `PSC = 47` then divides that by 48. `stepper_hal.c` derives this at
  runtime rather than hardcoding it (`timer_tick_hz()`), so retuning the clock
  tree in CubeMX does not silently change every step rate. TIM4 (the buzzer)
  uses the same `PSC = 47` for the same reason — a 1 MHz tick that
  `buzzer.c` turns directly into `ARR = 1000000 / frequency_hz - 1`.

`MX_USB_DEVICE_Init()` running *before* `App_init()` matters only for the
USB_CDC variant: the host can enumerate the device and start filling the
receive ring while the discs are still homing. In the default build it is a
no-op either way — `App/Src/usb_device_stub.c` supplies it, because
`Core/Src/main.c` calls it unconditionally and cannot be edited to guard the
call (06).

Everything before `App_init()` is Core/ handing you two ready-to-use
`UART_HandleTypeDef*`, two ready-to-use `TIM_HandleTypeDef*`, a ready-to-use
`SPI_HandleTypeDef*`, a third `TIM_HandleTypeDef*` for the buzzer, and
correctly configured GPIO pins. Everything from `App_init()` onward is your
code.

## 2.3 `App_init()` — one-time setup

`app_main.c` brings the motors and discs up first, then — in the MODBUS
variant — the buzzer, buttons, reader and PLC link:

```c
void App_init(UART_HandleTypeDef *huart_m1, UART_HandleTypeDef *huart_m2,
              TIM_HandleTypeDef *htim_m1, TIM_HandleTypeDef *htim_m2,
              TIM_HandleTypeDef *htim_buzzer, SPI_HandleTypeDef *hspi_eth) {
  initialize_motors(huart_m1, huart_m2, htim_m1, htim_m2);
  (void)calibrate_zero_position();

  buzzer_init(htim_buzzer, APP_BUZZER_TIM_CHANNEL);
  buttons_init();
  reader_ui_init(&app_reader_io);
  plc_link_init(hspi_eth, &app_link_io);
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
        MP->>MP: fault_led_blink(FAULT_LED_CODE_MOTOR_01) — blink LED, keep going
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
     `fault_led_blink()` blinks the onboard LED with that motor's code (1 or
     2 short blinks, repeated 3 times — moved to `fault_led.c` so
     `plc_link.c` can share the same reporting mechanism for a third fault
     code; see [09-modbus-tcp-and-plc-link.md](09-modbus-tcp-and-plc-link.md))
     but **does not halt** — every config call above is write-only over a
     half-duplex UART line, so the motor still runs even if the RX half of
     the link is broken; only read-back and diagnostics are lost. The
     comment in the code names the likely fix (check the `PDN_UART` wiring).
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

This is the last blocking thing in startup before the buzzer and link come
up, and it takes several seconds per disc — the design goal is "never spin
forever inside `App_init()`", not "boot instantly"
([08](08-motion-and-homing.md) §8.7 has the arithmetic). A disc whose sensor
never changes state is *not* fatal: the search is bounded, the motor's blink
code is flashed, and startup continues — which is exactly why `app_main.c`
can afford to ignore the return value with `(void)`. The full routine, its
bounds and its rate limits are covered in
[08-motion-and-homing.md](08-motion-and-homing.md).

### 2.3.3 Buzzer, buttons and the PLC link — bring-up for the MODBUS variant

Three cheap calls and one that blocks:

- `buzzer_init(htim_buzzer, APP_BUZZER_TIM_CHANNEL)` just stores the timer
  handle and channel — TIM4 was already configured for PWM by
  `MX_TIM4_Init()`, so there is nothing left to do at init time. It runs
  before the link deliberately, so a W5500 that turns out to be missing can
  report that fact out loud from inside the next call.
- `buttons_init()` clears the three buttons' debounce state.
- `reader_ui_init(&app_reader_io)` resets the cursor, the current and
  pending packages, and the sound rate limiters. Nothing is rendered yet.
- `plc_link_init(hspi_eth, &app_link_io)` is the one that blocks, for about
  12 ms: it pulses the W5500's reset line low for `PLC_W5500_RESET_LOW_MS`
  and waits `PLC_W5500_BOOT_MS` after release
  ([w5500_stm32.c:84-89](../Lib/w5500/w5500_stm32.c#L84-L89)), then reads
  the chip's `VERSIONR` register and expects `0x04`
  ([plc_link.c:106-108](../App/Src/plc_link.c#L106-L108)). A W5500 that does
  not answer is reported with `FAULT_LED_CODE_ETHERNET` and the link enters
  `CHIP_FAULT`, retried later from the main loop — it does not stop startup,
  the same philosophy as a disc that fails to home. A W5500 that does answer
  applies the static network identity from `plc_config.h`
  (`wizchip_init()`, `wizchip_setnetinfo()`) and the link enters `LINK_WAIT`.

Full breakdown of the state machine this hands off to, including what each
state is waiting for: [09-modbus-tcp-and-plc-link.md §9.4](09-modbus-tcp-and-plc-link.md#94-the-plc_link-state-diagram).

**The first poll happens later, once the main loop is already running.**
`App_init()` does not wait for a cable, a TCP connection, or a PLC reply —
it only brings the chip up. From here, `plc_link_tick()` (2.4) has to be
called repeatedly before the link can progress: `LINK_WAIT` until the PHY
reports a live cable, then `CONNECTING` until the TCP handshake completes,
then `IDLE`, where the very first automatic poll goes out after at most
`PLC_POLL_INTERVAL_MS` (500 ms). Its answer is what first calls
`reader_ui_on_snapshot()` — the reader adopts that package silently, plays
`DATA_RECEIVED`, and waits at `cursor == -1` for the first `NEXT` press. On
real hardware with a cable already plugged in and a PLC already listening,
this whole sequence — cable up, TCP connected, first request, first reply —
typically finishes within a couple of poll intervals of the main loop
starting.

## 2.4 `App_run()` — the cooperative tick

Called back-to-back, forever, from `main()`'s `while(1)`. In the default
MODBUS build, each call services three things in a fixed order and returns:

```
App_run()                                      App/Src/app_main.c
 ├─ buttons_take_event()                → one flag, if any pending, else READER_EV_NONE
 │   └─ reader_ui_on_event(event)       → reader_ui.c: may render (blocks ~1 s) + play a sound (blocks up to 480 ms)
 ├─ plc_link_tick()                     → plc_link.c: connect / poll / timeout / reconnect, see 09 §9.4
 │   └─ on_snapshot() / on_fault()      → reader_ui_on_snapshot() / reader_ui_on_link_fault()
 │        (same call stack — may also render + play a sound)
 └─ reader_ui_tick()                    → advances the label dwell if it has elapsed; may render (blocks)
```

The order is deliberate, and stated in the module's own header comment:
buttons first, "because they are the only thing a person is waiting on"; the
link next, "so a poll goes out as soon as it is due"; the sequencer last,
"since its work is timer-driven and a pass of delay costs nothing"
([app_main.c:7-10](../App/Src/app_main.c#L7-L10)).

Three consequences fall out of that shape:

- **Two calls can block, both bounded.** A disc move (`braille_render_char`/
  `braille_render_dots`, ultimately `move_to_angle()`) takes up to about a
  second; a buzzer pattern takes up to 480 ms
  ([10-reader-ui-and-buzzer.md §10.5](10-reader-ui-and-buzzer.md#105-why-the-buzzer-blocks-and-the-five-patterns)).
  Both are deliberate exceptions to an otherwise non-blocking loop. Nothing
  is lost while either runs: button presses are latched by their EXTI
  callback the whole time, and the W5500's socket keeps whatever the PLC
  sent queued in its own 16 KB RX buffer until the next `plc_link_tick()`.
- **`plc_link_tick()` can itself trigger a render.** A poll answering while
  `reader_ui` is `AWAITING_POLL` calls `reader_ui_on_snapshot()` straight
  from inside `plc_link_tick()`'s call stack, which can render immediately —
  that is the right moment for the discs to move, not something deferred to
  a later pass.
- **Nothing is polled to make the motors turn**, exactly as in the USB_CDC
  variant: the busy-wait inside a render is waiting on the STEP timer
  interrupts, not driving anything itself. That's the next section.

The USB_CDC variant's loop is simpler and unchanged from before this
project added MODBUS support — one byte in, one echo out, one disc move —
and is covered in full in [06-usb-cdc.md](06-usb-cdc.md).

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
| `USB_LP_CAN1_RX0_IRQn` | 0 | `HAL_PCD_IRQHandler` → `CDC_Receive_FS` | USB_CDC variant: copies received bytes into the CDC ring buffer. MODBUS variant: the vector and `HAL_PCD_IRQHandler` stay linked (06), but the interrupt is never enabled, so this never runs. |
| `EXTI9_5_IRQn` | 0 | `HAL_GPIO_EXTI_IRQHandler` ×4 → `HAL_GPIO_EXTI_Callback` → `buttons_on_exti()` + `plc_link_on_exti()` | MODBUS variant only: latches a debounced button press, or notes (unused by design) that the W5500 asserted `ETH_INT` — see [09-modbus-tcp-and-plc-link.md §9.6](09-modbus-tcp-and-plc-link.md#96-eth_int-pb5-wired-owned-deliberately-unused) |
| `TIM2_IRQn` | 0 | `HAL_TIM_IRQHandler` → `Stepper_OnPulseComplete(motor 1)` | Counts one STEP pulse, stops the train on the last |
| `TIM3_IRQn` | 0 | same, motor 2 | " |
| `SysTick` | 0 | `HAL_IncTick` | Feeds `HAL_Delay()` / `HAL_GetTick()` |

Everything else — TMC2209 UART traffic (still polled, unchanged), SPI2
traffic to the W5500 (also polled, touched only from `plc_link_tick()`;
`w5500_stm32.c`'s critical-section callbacks are no-ops because nothing else
ever touches that bus), TIM4's buzzer PWM (retuned from the main loop, then
left to run in hardware with zero CPU involvement until the next retune),
the UTF-8 decoder (USB_CDC variant only), the reader's cursor and dwell
logic — runs in the main loop.
