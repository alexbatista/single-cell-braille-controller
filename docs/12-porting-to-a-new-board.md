# 12. Porting to a new board

This document is the checklist for moving the firmware from the Blue Pill
prototype to a new PCB that keeps the same three chips (STM32F103C8, two
TMC2209, W5500) with a different layout and pinout. Changing the MCU family or
replacing a driver chip is out of scope here; it would also require a new HAL,
startup file, linker script and new ports for `Lib/tmc2209` and `Lib/w5500`.

**Source files:** `single-cell-braille-controller.ioc`, `Core/Inc/main.h`,
`Core/Src/gpio.c`, `Core/Src/tim.c`, `App/Src/motion_planner.c`,
`App/Src/app_main.c`, `App/Inc/plc_config.h`, `App/Src/fault_led.c`,
`tools/gen_wiring.py`.

Read [02-mcu-resources.md](02-mcu-resources.md) first: it lists every pin and
peripheral the firmware uses today.

## 12.1 Rules for changing the pinout

All pin changes are made in the `.ioc` with STM32CubeMX (or the STM32 VS Code
extension), followed by code generation. Never edit `Core/` outside `USER CODE`
blocks by hand: the next generation overwrites it
([01-architecture.md](01-architecture.md#11-layers)).

**Keep the labels.** The application never uses port/pin literals; it uses the
`<LABEL>_Pin` / `<LABEL>_GPIO_Port` macros CubeMX generates from the GPIO
labels. If every signal keeps its label, moving it to another pin needs no
application change. The labels the application expects:

| Label | Required mode |
|---|---|
| `STEP_MOTOR01`, `STEP_MOTOR02` | timer PWM channel output (see 12.2) |
| `DIR_MOTOR01`, `DIR_MOTOR02` | GPIO output |
| `ENABLE_MOTOR01`, `ENABLE_MOTOR02` | GPIO output, initial level **high** |
| `TX_TO_MOTOR01`/`RX_TO_MOTOR01`, `TX_TO_MOTOR02`/`RX_TO_MOTOR02` | USART TX/RX, one USART per driver |
| `ZERO_MOTOR01`, `ZERO_MOTOR02` | GPIO input, pull-down (or an external pull-down) |
| `ETH_NSS` | GPIO output, initial level **high** |
| `ETH_RESET` | GPIO output, initial level **high** |
| `ETH_INT` | EXTI falling edge, pull-up |
| `ETH_SPI2_SCK`, `ETH_SPI2_MISO`, `ETH_SPI2_MOSI` | SPI2 (any SPI works, see 12.3) |
| `BUZZER` | timer PWM channel output |
| `BTN_NEXT`, `BTN_PREV`, `BTN_REPEAT` | EXTI falling edge, pull-up |
| `LED` | GPIO output |

## 12.2 Things that are not just a label

| Item | Where it is fixed | What to change if it moves |
|---|---|---|
| STEP timers and channels | `initialize_motors()` passes `TIM_CHANNEL_2` and `TIM_CHANNEL_4`; `HAL_TIM_PWM_PulseFinishedCallback()` maps `TIM2` → motor 01, `TIM3` → motor 02; `App_init()` receives `&htim2`, `&htim3` from `main.c` | the channel constants, the instance test, and the handles in the `USER CODE 2` call. Each motor needs **its own timer**: the pulse counter is per timer interrupt. |
| STEP timer clock | derived at run time by `timer_tick_hz()` | nothing, if the timer is on APB1. A timer on APB2 (TIM1) needs `timer_tick_hz()` extended to read PCLK2. |
| Buzzer timer and channel | `APP_BUZZER_TIM_CHANNEL` in `app_main.c`; `&htim4` in `main.c`; tick derived by `buzzer_timer_tick_hz()` | the channel constant and handle; same APB1/APB2 remark |
| TMC2209 USARTs | `&huart2`, `&huart3` passed from `main.c` | the handles in the `App_init()` call |
| W5500 SPI | `&hspi2` passed from `main.c` | the handle; keep mode 0, MSB first, 8-bit, software NSS |
| EXTI lines | `HAL_GPIO_EXTI_Callback()` dispatches by pin number to `buttons_on_exti()` and `plc_link_on_exti()` | nothing in the application. Pins with the same number on different ports share one EXTI line and cannot both be interrupts. |
| Interrupt priorities | CubeMX NVIC settings | keep the application interrupts at equal priority, all higher (numerically lower) than SysTick (see [02-mcu-resources.md](02-mcu-resources.md#24-interrupts)) |
| Motor direction | `APP_MOTOR_DIR_INVERTED` in `motion_planner.c` | set per the new mechanism ([05-motion-and-homing.md](05-motion-and-homing.md#54-which-way-is-forward--app_motor_dir_inverted)) |
| Network identity | `plc_config.h` | a unique MAC per board if several share a network |

## 12.3 Electrical checklist for the PCB

- **TMC2209 UART:** MCU TX → 1 kΩ → PDN_UART; MCU RX → PDN_UART directly.
  MS1 = MS2 = GND (address 0) unless the drivers share a USART. VCC_IO from
  3.3 V. VREF has no effect in this firmware (current is set over UART), so a
  potentiometer is not needed ([04-tmc2209.md](04-tmc2209.md)).
- **ENN pull-ups:** keep the drivers disabled while the MCU is in reset. The
  internal pull-up is only active after `MX_GPIO_Init()`; an external pull-up
  to 3.3 V holds ENN high from power-on.
- **STEP lines:** low when idle; the timer output drives them low when stopped.
- **ZERO sensors:** active high with the tab in the slot. An external
  pull-down is advisable on a PCB with long sensor cables.
- **Buttons:** to GND, internal pull-ups. Hardware debounce is not required
  (the firmware debounces press and release), but an RC filter does no harm.
- **Buzzer:** low-side NPN (BC368 on the prototype) with a 1 kΩ base resistor
  from the timer pin, buzzer + to 5 V. A passive piezo is required.
- **W5500:** SPI traces short enough for 12 Mbit/s; RSTn driven by the MCU
  (not tied high) so the firmware can reset the chip on every retry.
- **LED polarity:** `fault_led.c` and `MX_GPIO_Init()` treat the LED as active
  high (`GPIO_PIN_SET` = on, boots off). Many Blue Pill boards wire PC13 active
  low. On the new PCB either wire it active high, or swap the `GPIO_PIN_SET` /
  `GPIO_PIN_RESET` levels in `fault_led_blink()` and the LED's initial level in the `.ioc`.
- **Power:** 12 V for the TMC2209 VM pins, 5 V for the buzzer, 3.3 V for the
  MCU, W5500, TMC2209 VCC_IO and the slot sensors; one common ground. The
  AMS1117 on the prototype dissipates roughly (12 − 3.3) V × load; a buck
  converter is preferable on a PCB.
- **SWD:** bring out SWDIO, SWCLK, GND, 3.3 V (and NRST) for flashing.
- **USB:** only needed for the USB_CDC bench variant.

## 12.4 Regenerate the wiring diagram

`docs/images/wiring.svg` is generated by `tools/gen_wiring.py`, which holds the
pin assignment in two lists (`TOP` and `BOT`, the Blue Pill headers) and in the
`wire(...)` calls. For a PCB with its own schematic the diagram can be retired;
while modules are still wired by hand, update the script and run:

```bash
python3 tools/gen_wiring.py docs/images/wiring.svg
```

Keep the pin table in the root `README.md` in step with it.

## 12.5 First power-up of a new board

Bring the subsystems up in an order where each step can be judged on its own;
the procedure and the commands are in
[13-build-test-debug.md](13-build-test-debug.md#138-bring-up-sequence).

1. Flash over SWD; LED off at rest.
2. Drivers: no blink code 1 or 2 at start-up; discs turn during homing.
3. Homing: both discs stop at their tab; no blink code.
4. W5500: no blink code 3; the device answers ping on its static IP.
5. Buzzer: the link-fault sound with no PLC present.
6. Buttons: each press produces the expected sound or glyph.
7. End to end against `tools/plc_sim.py`, then against the PLC.
