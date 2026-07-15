# 5. Is this overengineered?

Short answer: **the layering itself, no — but there are two small spots
worth knowing about.** Here's the evidence for both halves of that answer.

## 5.1 Why the layering is not the problem

"Overengineering" in most codebases means indirection that doesn't pay for
itself: interfaces with one implementation, factories building factories,
config for things that never vary. Measured against that bar:

- **Every abstraction here resolves to one function-pointer call.** No
  virtual dispatch chains, no dynamic allocation, no runtime type
  switching. [02-execution-flow.md §2.5](02-execution-flow.md#25-one-microstep-all-the-way-down)
  traces a full `Stepper_StepPulseBegin` call down to the single `BSRR`
  register store it compiles to — the "cost" of the abstraction is one
  indirect call, decided once at init time, not per pulse.
- **The pattern is used exactly twice, and it's the same pattern both
  times.** `Lib/tmc2209` (core/port) and `App/stepper` (core/port) aren't
  two different abstractions to learn — once you understand one, you
  understand the other. See [01-architecture-layers.md](01-architecture-layers.md#why-the-core--port-split-twice).
- **It's buying something real: host-testability.** `stepper.c` and
  `tmc2209.c` contain zero STM32 headers. That means the logic — step
  counting, direction tracking, register-value math — can be compiled and
  unit tested on your laptop with a plain C compiler, without touching a
  board or a debugger. For an embedded project, that's a legitimate,
  non-speculative payoff, not architecture-for-architecture's-sake.
- **It mirrors a boundary that already exists in the vendored library.**
  You didn't invent the core/port split — `Lib/tmc2209` (third-party code)
  already draws that line. Applying the same shape to your own `stepper`
  module means there's one mental model for "where does hardware-specific
  code live" across the whole codebase, instead of one convention for
  vendored code and a different one for your own.
- **Bypassing ST's HAL for GPIO/UART hot paths is a deliberate, documented
  trade, not extra layers.** `stepper_hal.c` and `tmc2209_stm32.c` go
  *around* `Drivers/STM32F1xx_HAL_Driver`, not through an additional layer
  on top of it — direct `BSRR`/register access is actually *fewer* function
  calls than `HAL_GPIO_WritePin`/`HAL_UART_Transmit` would be, chosen
  because STEP pulses and UART polling both happen often enough for the
  HAL's parameter checking to matter.

If you compare this against just writing everything as one flat file that
pokes `GPIOA->BSRR` directly from `App_run()` — the layering adds maybe 100
lines of indirection (`stepper.c` + `stepper_hal.c` combined) in exchange
for the whole motion module being independently testable and swappable to
another MCU later. For a project that's explicitly named after a
mechanical *actuator system* (braille cells, likely more motors and
modules coming), that trade reads as appropriately scoped, not premature.

## 5.2 Where it's fair to say "a little, yes"

Two things in the current code are complexity that isn't pulling its
weight *yet* — worth knowing so you don't mistake them for something
you're missing:

### `app_main.c` hand-duplicates four structs per motor

```c
static tmc2209_stm32_t tmc_port_motor_01;
static tmc2209_t tmc_motor_01;
static Stepper_Hal_Stm32_t stepper_hal_motor_01;
static Stepper_t stepper_motor_01;
// ... repeated verbatim for _motor_02
```

Plus `motor_driver_init()` and `motor_stepper_init()` are called twice with
parallel argument lists. This isn't wrong — with exactly two motors it's
arguably the most readable option — but it's the one place in the App
layer where the *lack* of an abstraction (e.g. a small `MotorAxis` struct
bundling all four and an array of two) is starting to cost readability
rather than save it. If a third motor ever gets added, that's the moment to
revisit it; today, with two, it's a judgment call either way.

### `Stepper_Enable`/`Stepper_Disable`/`Stepper_IsEnabled` are currently dead weight

`app_main.c` never calls any of them. Both stepper instances are
initialized with `enable_port = NULL` (`motor_stepper_init()`,
`App/Src/app_main.c:84-94`), because the `ENN` enable line for each motor
is owned by that motor's `tmc2209_t` instance instead
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
`Stepper_Enable` — there isn't one yet.

### Not actual complexity: the placeholders

`motion_planner.c` and `braille_disc.c` are one-line typedef stubs (see
[01-architecture-layers.md](01-architecture-layers.md)). They're listed in
`CMakeLists.txt` already so the build doesn't need touching once real code
lands, but there is nothing to reverse-engineer in them today — don't read
them as abstractions-in-waiting, they're just empty files with a build
target reserved.

## 5.3 The one-sentence version

The core/port split is a standard embedded pattern (hardware abstraction
via function pointers) applied consistently and cheaply; the actual soft
spots are ordinary duplication in `app_main.c` and one currently-unused API
surface in the stepper module — neither of which is what "overengineering"
usually means, but both are fair things to simplify once a third motor or
a differently-wired enable pin gives you a concrete reason to.
