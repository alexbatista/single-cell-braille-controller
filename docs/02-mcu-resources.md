# 2. MCU resources

This document is the reference for everything the firmware claims on the
STM32F103C8: clock tree, every pin, every peripheral and its configuration,
interrupt priorities, memory, and what happens when something fails before the
application can report it. It is the starting point for porting to a new
board ([12-porting-to-a-new-board.md](12-porting-to-a-new-board.md)).

**Source files:** `single-cell-braille-controller.ioc`, `Core/Src/main.c`
(`SystemClock_Config()`), `Core/Src/gpio.c`, `Core/Src/usart.c`,
`Core/Src/spi.c`, `Core/Src/tim.c`, `Core/Src/stm32f1xx_it.c`,
`Core/Src/stm32f1xx_hal_msp.c`, `Core/Inc/main.h`,
`Core/Inc/stm32f1xx_hal_conf.h`, `STM32F103XX_FLASH.ld`,
`cmake/gcc-arm-none-eabi.cmake`.

## 2.1 Clock tree

Configured by `SystemClock_Config()`:

| Clock | Source / divider | Frequency |
|---|---|---|
| HSE | 8 MHz crystal (PD0/PD1) | 8 MHz |
| PLL | HSE × 6 (`RCC_PLL_MUL6`) | 48 MHz |
| SYSCLK, HCLK | PLL, AHB ÷1 | 48 MHz |
| PCLK1 (APB1) | HCLK ÷2 | 24 MHz |
| APB1 timer clock (TIM2, TIM3, TIM4) | PCLK1 × 2, because the APB1 prescaler is not 1 | 48 MHz |
| PCLK2 (APB2) | HCLK ÷1 | 48 MHz |
| USB | PLL ÷1 | 48 MHz |
| Flash wait states | `FLASH_LATENCY_1` | — |

HSI is also left on. 48 MHz was chosen because the USB peripheral needs a
48 MHz clock from the PLL; the MODBUS variant keeps the same tree.

Two modules derive their timer tick from this tree at run time instead of
assuming it: `timer_tick_hz()` in `stepper_hal.c` (TIM2, TIM3) and
`buzzer_timer_tick_hz()` in `buzzer.c` (TIM4). Both read `HAL_RCC_GetPCLK1Freq()`
and the APB1 prescaler bits in `RCC->CFGR`, then divide by the timer's `PSC + 1`.
With the values above and `PSC = 47`, each timer counts at 1 MHz.

## 2.2 Pin map

Labels are the `*_Pin` macro names in `Core/Inc/main.h`, which the application
uses everywhere instead of port/pin literals.

| Pin | Label | Mode | Pull | Level after `MX_GPIO_Init()` | Owner |
|---|---|---|---|---|---|
| PA1 | `STEP_MOTOR01` | TIM2_CH2, AF push-pull | — | low (timer channel off) | `stepper_hal` (motor 01) |
| PA2 | `TX_TO_MOTOR01` | USART2_TX | — | — | `Lib/tmc2209` port (motor 01) |
| PA3 | `RX_TO_MOTOR01` | USART2_RX | — | — | `Lib/tmc2209` port (motor 01) |
| PA4 | `DIR_MOTOR01` | output push-pull | none | low | `stepper_hal` (motor 01) |
| PA5 | `ENABLE_MOTOR01` | output push-pull | pull-up | **high** (driver disabled) | `Lib/tmc2209` port (motor 01) |
| PA7 | `DIR_MOTOR02` | output push-pull | none | low | `stepper_hal` (motor 02) |
| PA8 | `ZERO_MOTOR02` | input | **pull-down** | — | `motion_planner` (homing) |
| PA9 | `ZERO_MOTOR01` | input | **pull-down** | — | `motion_planner` (homing) |
| PA10 | `ETH_RESET` | output push-pull | none | **high** (W5500 running) | `w5500_stm32` |
| PA11 / PA12 | — | USB DM / DP | — | — | USB_CDC variant only |
| PA13 / PA14 | — | SWDIO / SWCLK | — | — | debug probe |
| PB0 | `ENABLE_MOTOR02` | output push-pull | pull-up | **high** (driver disabled) | `Lib/tmc2209` port (motor 02) |
| PB1 | `STEP_MOTOR02` | TIM3_CH4, AF push-pull | — | low (timer channel off) | `stepper_hal` (motor 02) |
| PB5 | `ETH_INT` | EXTI, falling edge | pull-up | — | `plc_link` (owned, unused) |
| PB6 | `BUZZER` | TIM4_CH1, AF push-pull | — | low | `buzzer` |
| PB7 | `BTN_REPEAT` | EXTI, falling edge | pull-up | — | `buttons` |
| PB8 | `BTN_NEXT` | EXTI, falling edge | pull-up | — | `buttons` |
| PB9 | `BTN_PREV` | EXTI, falling edge | pull-up | — | `buttons` |
| PB10 | `TX_TO_MOTOR02` | USART3_TX | — | — | `Lib/tmc2209` port (motor 02) |
| PB11 | `RX_TO_MOTOR02` | USART3_RX | — | — | `Lib/tmc2209` port (motor 02) |
| PB12 | `ETH_NSS` | output push-pull | pull-up | **high** (deselected) | `w5500_stm32` |
| PB13 | `ETH_SPI2_SCK` | SPI2_SCK | — | — | `w5500_stm32` |
| PB14 | `ETH_SPI2_MISO` | SPI2_MISO | — | — | `w5500_stm32` |
| PB15 | `ETH_SPI2_MOSI` | SPI2_MOSI | — | — | `w5500_stm32` |
| PC13 | `LED` | output push-pull | none | low (off) | `fault_led` |
| PD0 / PD1 | — | HSE oscillator | — | — | clock |

Polarities the application depends on:

- **ENN (PA5, PB0) is active low.** Driving it high disables the TMC2209
  output stage. The pins boot high so the motors are unpowered until
  `initialize_motors()` has configured the drivers.
- **ZERO inputs read high while a disc's tab is in the sensor slot.** The
  internal pull-downs make an unconnected or unpowered sensor read low, so
  homing fails with a blink code instead of "finding" zero immediately.
- **Buttons are active low.** Each closes its pin to GND; the internal pull-up
  is the only resistor.
- **The LED is driven active high** (`GPIO_PIN_SET` = on in `fault_led.c`).
  Many Blue Pill boards wire PC13 active low; on such a board the blink codes
  appear as dark pulses on a lit LED. Check this on any new board.

`__HAL_AFIO_REMAP_SWJ_NOJTAG()` in `HAL_MspInit()` keeps SWD and releases the
JTAG-only pins (PA15, PB3, PB4), which are unused.

## 2.3 Peripherals

| Peripheral | Configuration | Used by |
|---|---|---|
| USART2 | 115200 baud, 8N1, TX+RX, no flow control, 16× oversampling | TMC2209 motor 01 (single-wire, see [04-tmc2209.md](04-tmc2209.md)) |
| USART3 | same as USART2 | TMC2209 motor 02 |
| SPI2 | master, full duplex, 8-bit, CPOL 0 / CPHA 0 (mode 0), MSB first, software NSS, prescaler ÷2 → 12 Mbit/s from 24 MHz PCLK1 | W5500 |
| TIM2 | PSC 47 (1 MHz tick), up-counting, ARR preload on, CH2 PWM mode 1 | STEP motor 01 |
| TIM3 | PSC 47 (1 MHz tick), up-counting, ARR preload on, CH4 PWM mode 1 | STEP motor 02 |
| TIM4 | PSC 47 (1 MHz tick), up-counting, ARR preload **off**, CH1 PWM | buzzer |
| EXTI 5–9 | falling edge on PB5, PB7, PB8, PB9 | W5500 INT (unused), buttons |
| USB FS | CDC device | USB_CDC variant only |
| SysTick | 1 ms | `HAL_GetTick()`, `HAL_Delay()` |
| DWT CYCCNT | enabled by `tmc2209_stm32_hal_init()` | TMC2209 microsecond delays |

The `Period` and `Pulse` values that CubeMX writes for TIM2–TIM4 (Period 999,
999 and 1000; Pulse 500) are placeholders. `stepper_hal.c` and `buzzer.c` write `ARR` and `CCR`
before every pulse train or tone.

## 2.4 Interrupts

`HAL_Init()` sets priority grouping 4 (all four bits are pre-emption priority,
no sub-priority).

| IRQ | Priority | Handler → application | Fires in normal operation |
|---|---|---|---|
| `EXTI9_5_IRQn` | 0 | `HAL_GPIO_EXTI_IRQHandler` for PB5, PB7, PB8, PB9 → `HAL_GPIO_EXTI_Callback()` in `app_main.c` → `buttons_on_exti()` and `plc_link_on_exti()` | yes (buttons) |
| `TIM2_IRQn` | 0 | `HAL_TIM_IRQHandler(&htim2)` → `HAL_TIM_PWM_PulseFinishedCallback()` in `motion_planner.c` → `Stepper_OnPulseComplete()` | yes, once per STEP pulse of motor 01 |
| `TIM3_IRQn` | 0 | same for motor 02 | yes, once per STEP pulse of motor 02 |
| `USART2_IRQn`, `USART3_IRQn` | 0 | `HAL_UART_IRQHandler` | **no**: enabled by CubeMX, but the TMC2209 port polls the USART and never enables an interrupt source |
| `USB_LP_CAN1_RX0_IRQn` | 0 | `HAL_PCD_IRQHandler` | USB_CDC variant only |
| `SysTick_IRQn` | 15 (`TICK_INT_PRIORITY`) | `HAL_IncTick()` | yes, 1 kHz |

Consequences:

- All application interrupts share priority 0, so none pre-empts another; a
  burst of button edges can delay a STEP callback by the length of one ISR,
  never by more.
- SysTick has the lowest priority. It is delayed (not lost) while a priority-0
  ISR runs, so `HAL_GetTick()` stays accurate at the millisecond scale used
  here. No ISR may call `HAL_Delay()`, which would wait forever for a tick that
  cannot pre-empt it.
- ISRs only set flags or update counters. All decisions run in the main loop
  ([03-execution-flow.md](03-execution-flow.md)).

## 2.5 Memory

Linker script `STM32F103XX_FLASH.ld`: 64 KB flash at `0x08000000`, 20 KB RAM at
`0x20000000`, minimum heap `0x200` (512 B) and stack `0x400` (1 KB) reserved at
the end of RAM. Linked with `--specs=nano.specs`, `-ffunction-sections
-fdata-sections` and `--gc-sections`; `-fstack-usage` writes a `.su` file per
object and the link prints a memory summary.

Measured at the commit that introduced this document (`arm-none-eabi-gcc`
14.3.1, `--print-memory-usage`; RAM includes the reserved heap and stack):

| Build | Flash | RAM |
|---|---|---|
| Release (`-Os`), MODBUS | 22 360 B (34 %) | 3 328 B (16 %) |
| Debug (`-O0 -g3`), MODBUS | 45 564 B (70 %) | 3 328 B (16 %) |
| Release, USB_CDC | 22 104 B (34 %) | 7 312 B (36 %) |
| Debug, USB_CDC | 47 604 B (73 %) | 7 312 B (36 %) |

The Debug build is the one that limits headroom: new features should be sized
against it, or development should move to Release-with-symbols.

## 2.6 Silent failure paths

Some failures happen before or outside the application's reporting:

| Condition | What happens |
|---|---|
| Any `MX_*_Init()` or clock configuration returns an error | `Error_Handler()`: interrupts disabled, infinite loop. No LED, no sound. |
| HardFault, MemManage, BusFault, UsageFault | Default handlers in `stm32f1xx_it.c`: infinite loop. |
| `assert_param()` | Compiled out (`USE_FULL_ASSERT` is not defined). |
| Main loop stalls | Nothing recovers it: no independent watchdog (IWDG) is enabled. |

Faults the application does report (TMC2209 not answering, homing failure,
W5500 absent) are listed with their blink codes in
[13-build-test-debug.md](13-build-test-debug.md#137-blink-codes).
