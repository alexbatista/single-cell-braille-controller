# 5. Is this overengineered?

Short answer: **the layering itself, no — but there are three small spots
worth knowing about.** Here's the evidence for both halves of that answer.

## 5.1 Why the layering is not the problem

"Overengineering" in most codebases means indirection that doesn't pay for
itself: interfaces with one implementation, factories building factories,
config for things that never vary. Measured against that bar:

- **Every abstraction here resolves to one function-pointer call.** No
  virtual dispatch chains, no dynamic allocation, no runtime type
  switching. [02-execution-flow.md §2.5](02-execution-flow.md#25-one-microstep-all-the-way-down)
  traces a full move from `start_move()` down to the two timer registers and
  the `BSRR` store it becomes — the "cost" of the abstraction is one indirect
  call per pulse, on top of an interrupt the hardware raises anyway.
- **The pattern is used exactly twice, and it's the same pattern both
  times.** `Lib/tmc2209` (core/port) and `App/stepper` (core/port) aren't
  two different abstractions to learn — once you understand one, you
  understand the other. See [01-architecture-layers.md](01-architecture-layers.md#why-the-core--port-split-twice).
- **The host-testability payoff has actually been collected.** This is the
  argument that usually stays theoretical, and here it didn't: `stepper.c`
  contains zero STM32 headers, so the homing routine and the step-budget
  accounting were both exercised on a host with stub HALs — a fake pulse train
  and a simulated sensor flag — before they ever ran on the board. Checking
  "does homing land on the same position from 50 different starting angles"
  is a ten-second host test and a genuinely painful bench test.
- **The seam decides where facts live, not just where code lives.** The
  `invert_dir` flag is the clearest case: "which way does DIR turn this disc"
  is a wiring fact, so it lives in the port and the planner keeps the single
  invariant "positive steps increase the angle." Without that seam the same
  fact would have been smeared across `steps_to_angle()`, the homing
  directions, and every future caller — which is exactly the shape the bug
  fixed in `3d637d9` would have taken.
- **It mirrors a boundary that already exists in the vendored library.**
  You didn't invent the core/port split — `Lib/tmc2209` (third-party code)
  already draws that line. Applying the same shape to your own `stepper`
  module means there's one mental model for "where does hardware-specific
  code live" across the whole codebase.
- **Bypassing ST's HAL for the GPIO/UART hot paths is a deliberate, documented
  trade, not extra layers.** The DIR/ENN writes in `stepper_hal.c` and the
  UART polling in `tmc2209_stm32.c` go *around*
  `Drivers/STM32F1xx_HAL_Driver`, not through an additional layer on top of
  it. Note that the timer setup in the same file *does* go through the HAL —
  the split is by frequency, not by taste: per-pulse code avoids the HAL,
  per-move code doesn't bother.

If you compare this against writing everything as one flat file that pokes
`GPIOA->BSRR` and `TIM2->ARR` directly from `App_run()` — the layering adds
roughly 200 lines of indirection (`stepper.c` + `stepper_hal.c`) in exchange
for a motion module that is independently testable and portable, and for a
planner that can be read without knowing anything about timers.

## 5.2 Where it's fair to say "a little, yes"

Three things in the current code are complexity that isn't pulling its
weight *yet* — worth knowing so you don't mistake them for something you're
missing.

### `motion_planner.c` hand-duplicates four structs per motor

```c
static tmc2209_stm32_t tmc_port_motor_01;
static tmc2209_t tmc_motor_01;
static Stepper_Hal_Stm32_t stepper_hal_motor_01;
static Stepper_t stepper_motor_01;
// ... repeated verbatim for _motor_02
```

Plus `motor_driver_init()` and `motor_stepper_init()` are called twice with
parallel argument lists, `HAL_TIM_PWM_PulseFinishedCallback()` dispatches with
an `if (TIM2) … else if (TIM3)`, and `calibrate_zero_position()` repeats the
same home-then-assign block per disc. This isn't wrong — with exactly two
motors it's arguably the most readable option — but it's the one place in the
App layer where the *lack* of an abstraction (a small `MotorAxis` struct
bundling the four instances plus its DIR/ZERO pins, and an array of two) is
starting to cost readability rather than save it. Note that the duplication
has grown since homing landed: it now shows up in four places rather than two.
If a third motor is ever added, that's the moment to collapse it.

### `Stepper_Enable`/`Stepper_Disable`/`Stepper_IsEnabled` are still dead weight

Nothing in `App/` calls any of them. Both stepper instances are initialized
with `enable_port = NULL` (`motor_stepper_init()` in
`App/Src/motion_planner.c`), because the `ENN` enable line for each motor is
owned by that motor's `tmc2209_t` instance instead
(`tmc2209_enable()`/`ENN`, see [04-tmc2209-howto.md](04-tmc2209-howto.md)).
So `Stepper_t.enabled` exists, has an API, and is exercised by
`Stepper_Init()` — but nothing in this project's current wiring ever flips
it or reads it back. It's not incorrect (the field defaults to `false` and
is otherwise inert), and it's clearly there so the stepper module works
standalone for a driver wired differently (enable pin on the stepper side
instead of the driver side) — but as of today, if you're tracing "what
actually enables the motors," the answer is entirely in
[04-tmc2209-howto.md §4.2](04-tmc2209-howto.md#42-setup-call-sequence-used-by-this-project),
not in the stepper module. Don't spend time looking for a caller of
`Stepper_Enable` — there isn't one.

### `motion_planner.c` now holds three jobs, not one

The file brings up two TMC2209s over UART, converts angles into shortest-path
microstep moves, *and* runs the homing state machine — around 350 lines
covering three unrelated concerns, with the blink-code fault reporter shared
between them. Nothing is tangled (the homing section is cleanly separated and
only uses `start_move()`/`Stepper_IsBusy()`), but "motion planner" has quietly
become "everything about motors." If this file grows again — a second cell, an
acceleration ramp, a jog command — splitting driver bring-up out of it is the
natural first cut, and the fact that homing already talks to the steppers
through the same two helpers the planner uses means the seam is easy to find.

### Not actual complexity: the derived constants

`motion_planner.c` and `stepper_hal.c` define constants in terms of other
constants — `APP_STEP_RATE_HZ` from a rotation time, `APP_HOMING_SLACK_STEPS`
from an angle, `STEP_TIMER_MAX_PERIOD_TICKS` from a counter width — where a
plain number would have been shorter. That is a deliberate policy, not
ceremony: it keeps the intent visible ("one revolution per second", "45° of
slack", "the widest period a 16-bit counter can express") and it means
changing the microstepping or retuning the clock tree doesn't silently break
the arithmetic three files away. Read those blocks as documentation that the
compiler checks.

## 5.3 The one-sentence version

The core/port split is a standard embedded pattern (hardware abstraction via
function pointers) applied consistently and cheaply, and it has already paid
for itself twice — once in off-target tests, once by giving a mirrored-gearing
bug exactly one place to be fixed; the actual soft spots are ordinary
duplication across the two motors, one currently-unused API surface, and a
planner file that has accumulated a third responsibility — all three worth
tidying when a concrete reason arrives, none of them what "overengineering"
usually means.
