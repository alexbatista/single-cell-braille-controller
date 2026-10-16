# 5. Motion and homing

This document covers everything between "show this dot pattern" and the two
discs standing at the right angles: the units the firmware works in, the
stepper module and its hardware pulse train, shortest-path moves, the
direction inversion, and the homing routine that gives absolute angles their
meaning. How a character becomes a pair of angles is in
[06-braille-encoding.md](06-braille-encoding.md).

**Source files:** `App/Src/motion_planner.c`, `App/Inc/motion_planner.h`,
`App/Src/stepper.c`, `App/Inc/stepper.h`, `App/Src/stepper_hal.c`,
`App/Inc/stepper_hal.h`, `Core/Src/tim.c`.

## 5.1 Frames of reference

| Frame | Unit | Range | Used by |
|---|---|---|---|
| Braille cell | 6-dot bitmask → per-disc weight | weight 0–3 (disc 1), 0–15 (disc 2) | `braille_disc.c` |
| Disc angle | tenths of a degree | 0–3599 | `disk_angles_t`, `move_to_angle()` |
| Microsteps | signed pulse count | 1600 per revolution | `Stepper_t.position`, STEP timers |

Constants in `motion_planner.c`:

| Constant | Value | Meaning |
|---|---|---|
| `APP_FULL_STEPS_PER_REV` | 200 | 1.8° motor |
| `APP_MICROSTEPS` | 8 | TMC2209 MRES |
| `APP_MICROSTEPS_PER_REV` | 1600 | derived |
| `APP_ANGLE_TENTHS_PER_REV` | 3600 | angle unit |
| `APP_ROTATION_TIME_MS` | 1000 | one revolution per second |
| `APP_STEP_RATE_HZ` | 1600 | derived: microsteps per revolution per second |

Every disc position is a multiple of 22.5° (225 tenths). One microstep is
3600 / 1600 = 2.25 tenths (0.225°), so one position is exactly 100
microsteps and no move ever needs rounding. 22.5° is 12.5 full steps, which
is why at least 2 microsteps per full step are required; any power of two from
2 to 256 keeps the arithmetic exact. Tenths of a degree are used instead of
degrees because 22.5 is not an integer.

## 5.2 The stepper module

The stepper is split into a portable core and an STM32 port
([01-architecture.md](01-architecture.md#14-two-patterns-that-keep-hardware-at-the-edges)).

### Core — `stepper.h` / `stepper.c`

`Stepper_t` holds a copy of the `Stepper_Hal_t` callbacks, the signed
`position`, the `volatile remaining` step budget, the current direction and an
enable flag.

| Function | Behaviour |
|---|---|
| `Stepper_Init()` | Copies the HAL table, zeroes position and budget, DIR forward, stops any pulse train, disables the driver if an enable callback exists. |
| `Stepper_SetDirection()` | Stores the direction and writes DIR. Must be called before `Stepper_MoveSteps()`, never during a move. |
| `Stepper_MoveSteps(steps, pulse_hz)` | Ignored if `steps` or `pulse_hz` is 0 or a move is in progress. Otherwise sets `remaining = steps` and starts the pulse train. Returns immediately. |
| `Stepper_OnPulseComplete()` | Called from the timer ISR once per finished pulse: `position ± 1`, `remaining − 1`, stops the train at 0. A callback with `remaining == 0` (a pulse already in flight when the train stopped) is ignored. |
| `Stepper_IsBusy()` | `remaining != 0`. |
| `Stepper_GetPosition()` / `Stepper_SetPosition()` | Position counter; `Stepper_SetPosition()` is how homing defines zero. |
| `Stepper_Enable()`, `Stepper_Disable()`, `Stepper_IsEnabled()`, `Stepper_Stop()` | Available, unused: ENN belongs to the TMC2209 instance, and no move is ever aborted. |

### STM32 port — `stepper_hal.c`

`Stepper_Hal_Stm32_Init()` binds one `Stepper_Hal_Stm32_t` to a timer handle,
a PWM channel, a DIR pin, a DIR inversion flag and an optional ENN pin
(`NULL` here). It provides:

- **`pulse_train_start(pulse_hz)`** — computes the period in timer ticks from
  the clock tree (`timer_tick_hz()`, 1 MHz here), clamps it to
  `[STEP_TIMER_MIN_PERIOD_TICKS, STEP_TIMER_MAX_PERIOD_TICKS]` = [2, 65536],
  writes `ARR = ticks − 1` and `CCR = ticks / 2`, forces an update event so the
  preloaded registers take effect, clears `SR`, and starts the channel with
  `HAL_TIM_PWM_Start_IT()`. STEP rises at once (PWM mode 1, counter 0); the
  compare match ends the pulse and raises the interrupt that counts it.
- **`pulse_train_stop()`** — `HAL_TIM_PWM_Stop_IT()`. With the channel output
  disabled, the TIM2/TIM3 alternate-function output drives the pin low, so
  STEP idles low.
- **`dir_write(level)`** — writes `level`, or `!level` when `invert_dir` is
  set, through `BSRR`.

The upper clamp sets the slowest rate the hardware can produce:
1 MHz / 65536 ≈ **15.26 Hz**. A slower request runs at that floor instead of
wrapping `ARR` and `CCR` separately, which could put `CCR` above `ARR`, never
fire the compare interrupt and hang every caller waiting on
`Stepper_IsBusy()`. The fastest rate is 1 MHz / 2 = 500 kHz.

The ISR-to-instance mapping lives in `motion_planner.c`:
`HAL_TIM_PWM_PulseFinishedCallback()` calls `Stepper_OnPulseComplete()` for
`stepper_motor_01` when the instance is TIM2 and `stepper_motor_02` when it is
TIM3.

## 5.3 Moves — `move_to_angle()`

```c
void move_to_angle(disk_angles_t disk_angle);
```

1. For each disc, `steps_to_angle()` converts the absolute target angle to
   microsteps (`angle × 1600 / 3600`), reduces the current position modulo
   one revolution, and returns the signed delta folded into
   [−800, +800]: the shorter way round.
2. `start_move()` sets the direction from the sign and starts a train of
   `|delta|` pulses at `APP_STEP_RATE_HZ`. A delta of 0 starts nothing, so a
   disc that does not need to move stays still.
3. `wait_until_idle()` spins until neither stepper is busy.

Both discs move at the same time, each on its own timer, so a move lasts as
long as the longer of the two: at most 800 microsteps at 1600 Hz = **0.5 s**.
The call blocks for that time; see
[03-execution-flow.md](03-execution-flow.md#37-blocking-budget).

`position` is never wrapped. It grows without bound as moves accumulate, and
`steps_to_angle()` reduces it modulo one revolution each time. A signed 32-bit
counter reaches its limit after about 1.3 million revolutions.

## 5.4 Which way is forward — `APP_MOTOR_DIR_INVERTED`

The planner treats positive steps as increasing angle. On the prototype both
discs are geared so that the direction the TMC2209 calls forward turns them
towards **decreasing** braille weight. `APP_MOTOR_DIR_INVERTED` (`true`) is
passed to both `Stepper_Hal_Stm32_Init()` calls, and `dir_write()` inverts the
pin level, so every layer above sees positive = increasing angle.

A wrong setting does not show as "letters backwards". Each commanded angle θ is
reached as −θ, which is another valid position:

| Key | Commanded | Reached | Shown as |
|---|---|---|---|
| `a` | disc 1 at 22.5° (weight 1 = dot 1) | 337.5°, which disc 1 reads as weight 3 | dots 1 **and** 4 |
| `b` | disc 1 and disc 2 at 22.5° | 337.5° on both | weight 3 + weight 15: all six dots |

Disc 1 repeats its four combinations every 90°, so its errors alias more than
disc 2's. A rebuilt mechanism with the opposite gearing needs this constant set
to `false`; the homing search direction then also lands on the other edge of
the tab (section 5.6).

## 5.5 Why homing is required

Stepper control is open loop: `position` counts pulses sent, not where the
disc is. At power-up it is 0 wherever the disc happens to be, and every
absolute angle would be offset by that unknown amount. Each disc carries a tab
that passes through a slotted IR sensor once per revolution; homing turns each
disc until the tab's edge is detected and then declares that point zero.

| Signal | Pin | Configuration | Level with the tab in the slot |
|---|---|---|---|
| `ZERO_MOTOR01` | PA9 | input, pull-down | high |
| `ZERO_MOTOR02` | PA8 | input, pull-down | high |

The pull-downs matter: a disconnected or unpowered sensor reads low ("no tab"),
so homing fails visibly instead of succeeding at once at an arbitrary angle.

Zero is placed on index 0 of both discs, the all-flat space character, so after
homing the cell shows a blank without any offset constant
([06-braille-encoding.md](06-braille-encoding.md)).

## 5.6 Homing

`calibrate_zero_position()` homes disc 1, then disc 2. For each, `home_disc()`
runs four bounded searches with `seek_sensor()`, which moves one microstep at a
time and samples the sensor between steps:

| Phase | Direction | Until the sensor reads | Rate | Budget |
|---|---|---|---|---|
| 1. Walk off the tab | search (+) | low | `APP_HOMING_SEEK_RATE_HZ` = 400 Hz | `APP_HOMING_SEARCH_STEPS` = 1800 |
| 2. Find the tab | search (+) | high | 400 Hz | 1800 |
| 3. Back off | back-off (−) | low | 400 Hz | `APP_HOMING_SLACK_STEPS` = 200 |
| — | settle | — | — | `APP_HOMING_SETTLE_MS` = 50 ms |
| 4. Final approach | search (+) | high | `APP_HOMING_FINE_RATE_HZ` = 25 Hz | `APP_HOMING_APPROACH_STEPS` = 400 |

On success, `Stepper_SetPosition(stepper, APP_ZERO_POSITION_STEPS)` (0).

Why each phase exists:

1. A disc that powers up with its tab already in the slot would otherwise be
   zeroed wherever inside the slot it stopped, an error as wide as the tab.
2. Finds the leading edge at seek speed.
3. and 4. The edge is approached a second time slowly and from the same side,
   so the zero does not depend on how fast the disc arrived or on the
   sensor's hysteresis. Every edge that defines zero is detected while turning
   in `APP_HOMING_SEARCH_DIR`; the opposite direction is only used to leave the
   tab.

`APP_HOMING_FINE_RATE_HZ` (25 Hz) is kept above the 15.26 Hz STEP floor.

## 5.7 Bounds

| Constant | Value | Reason |
|---|---|---|
| `APP_HOMING_SEARCH_TIME_MS` | 4000 | One revolution in 4 s; sets the seek rate |
| `APP_HOMING_SEEK_RATE_HZ` | 400 Hz | 1600 / 4 s |
| `APP_HOMING_SLACK_TENTHS` | 450 (45°) | Expressed as an angle so it survives a microstep change |
| `APP_HOMING_SLACK_STEPS` | 200 | 45° in microsteps |
| `APP_HOMING_SEARCH_STEPS` | 1800 | One revolution + slack: a search this long has missed the tab |
| `APP_HOMING_APPROACH_STEPS` | 400 | Twice the slack: undoes the back-off and still reaches the edge |
| `APP_HOMING_FINE_RATE_HZ` | 25 Hz | Slow final approach, above the STEP floor |
| `APP_HOMING_SETTLE_MS` | 50 ms | Pause after reversing |

Worst case per disc, if every phase used its whole budget:
1800/400 + 1800/400 + 200/400 + 0.05 + 400/25 ≈ **25.6 s**. A healthy disc
homes in at most about one revolution at 400 Hz (4 s) plus a short approach.
Homing blocks `App_init()` for its whole duration.

## 5.8 When homing fails

`seek_sensor()` returns `false` only when its budget runs out with the sensor
in the wrong state. `calibrate_zero_position()` then blinks
`FAULT_LED_CODE_MOTOR_01` or `FAULT_LED_CODE_MOTOR_02` (the same codes as a driver that does not
answer), leaves that disc's position unchanged, still homes the other disc, and
returns `false`. `App_init()` ignores the return value and continues: a
sensor fault does not take away the PLC readout. Characters on an unhomed disc
are rendered relative to wherever it powered up.

Checklist for a disc that never homes:

1. **Sensor powered and wired?** With the pull-down an open D0 reads a
   constant low, which looks like "tab never arrives".
2. **Does the tab pass through the slot?** Turn the disc by hand and watch
   PA9/PA8 with a meter or in the debugger.
3. **Does the disc turn at all?** A driver that does not answer over UART
   blinks the same code from `initialize_motors()` earlier in start-up; count
   the blink sequences.
4. **Is the tab wider than 45°?** Phase 3 gives up after
   `APP_HOMING_SLACK_TENTHS`.

## 5.9 Adapting to a different mechanism

| Change | Edit |
|---|---|
| Motor with a different step angle | `APP_FULL_STEPS_PER_REV` |
| Different microstepping | `APP_MICROSTEPS` (see section 5.1 for the constraint) |
| Faster or slower character changes | `APP_ROTATION_TIME_MS` |
| Gearing reversed | `APP_MOTOR_DIR_INVERTED` |
| Tab not aligned with pattern index 0 | `APP_ZERO_POSITION_STEPS` (microsteps from the tab edge to index 0) |
| Zero on the other edge of the tab | swap the signs of `APP_HOMING_SEARCH_DIR` and `APP_HOMING_BACKOFF_DIR` |
| Wider tab | `APP_HOMING_SLACK_TENTHS` |
| Different STEP pins or timers | `.ioc` (pins must be timer-channel outputs, one timer per motor), the channel arguments in `initialize_motors()`, and the instance test in `HAL_TIM_PWM_PulseFinishedCallback()` |

No host test covers `stepper.c` or the homing routine; changes here are
verified on hardware.
