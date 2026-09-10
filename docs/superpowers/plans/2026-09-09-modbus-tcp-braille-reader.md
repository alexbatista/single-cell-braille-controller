# MODBUS TCP Braille Reader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the braille cell display an 11-signal boolean package read from a PLC over MODBUS TCP, navigated with three buttons and narrated by a buzzer, with the USB typed-character path preserved as a non-default build variant.

**Architecture:** `App_run()` becomes a cooperative tick that services debounced button events, a non-blocking W5500/MODBUS link state machine, and a dwell-timed presentation sequencer. All navigation logic lives in `reader_ui.c`, which touches no hardware and is driven through a function-pointer IO struct so the entire behaviour table is verifiable on the host with `gcc`. The only blocking calls are the existing disc move and bounded (≤400 ms) buzzer patterns.

**Tech Stack:** STM32F103C8T6, STM32Cube HAL (F1), CMake + Ninja + arm-none-eabi-gcc from ST bundles, WIZnet ioLibrary_Driver (W5500 over SPI2), host `gcc` for unit tests.

**Spec:** [docs/superpowers/specs/2026-09-09-modbus-tcp-braille-reader-design.md](../specs/2026-09-09-modbus-tcp-braille-reader-design.md) — read it alongside this plan; every task argues from a numbered section of it.

## Global Constraints

- **No magic numbers.** Every constant is a named `#define` or enum, declared once. Numeric literals at a use site are a review rejection. Sentinels `0` and `-1` for `cursor` are the documented exception (spec §6).
- **Target part:** STM32F103C8T6 — 64 KB flash, 20 KB RAM. Baseline before this work: Release 22 068 B flash / 7 312 B RAM; Debug 46 532 B flash (spec §11).
- **Toolchain is not on `PATH`.** Every firmware build step must first run:
  ```bash
  export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
  ```
  If those versions are gone, list `~/.local/share/stm32cube/bundles/` and use what is there.
- **Firmware build:** `cmake --preset Debug && cmake --build --preset Debug` (also `Release`). Host tests: `make -C tests`.
- **Never edit generated code outside `USER CODE` regions.** `Core/`, `USB_DEVICE/`, `cmake/stm32cubemx/CMakeLists.txt` and `single-cell-braille-controller.ioc` are CubeMX-owned. The top-level `CMakeLists.txt` is explicitly user-owned ("generated only once") and is the correct place for build surgery.
- **`App/Src/*.c` is globbed** by the top-level `CMakeLists.txt` with `CONFIGURE_DEPENDS`, so new App sources need no CMake edit.
- **C11**, `-Wall`, `-ffunction-sections -fdata-sections -Wl,--gc-sections` already enabled.
- **Doxygen comment style** matches the existing App headers: `@file`/`@brief`/`@param`/`@return`, `@defgroup` + `/** @} */` per module. Grouping blocks must balance — an unbalanced `@name`/`@{` has broken the Doxygen build here before.
- **Record `arm-none-eabi-size` output in every firmware commit message** so a flash regression is attributable to one task.
- **Commit after every task.** End each commit message with:
  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  ```

## File Structure

**Created — host test harness**
| Path | Responsibility |
|---|---|
| `tests/Makefile` | Builds and runs every host suite with system `gcc` |
| `tests/test_support.h` | `CHECK`/`CHECK_EQ` assert macros and the pass/fail counter |
| `tests/stubs/stm32f1xx_hal.h` | Minimal stand-in so App headers compile off-target |
| `tests/test_braille_dots.c` | Task 1 |
| `tests/test_plc_packet.c` | Task 4 |
| `tests/test_modbus_tcp.c` | Task 5 |
| `tests/test_reader_ui.c` | Task 6 |

**Created — App layer**
| Path | Responsibility |
|---|---|
| `App/Inc/fault_led.h`, `App/Src/fault_led.c` | `blink_warning()` + blink-code constants, one home for all three callers |
| `App/Inc/plc_config.h` | Every tunable number: network identity, PLC endpoint, coil range, timings |
| `App/Inc/plc_packet.h`, `App/Src/plc_packet.c` | The 11-field packet contract and snapshot accessors |
| `App/Inc/modbus_tcp.h`, `App/Src/modbus_tcp.c` | Pure MBAP+PDU codec for FC01 Read Coils |
| `App/Inc/reader_ui.h`, `App/Src/reader_ui.c` | Cursor, pending slot, presentation sequencer |
| `App/Inc/buzzer.h`, `App/Src/buzzer.c` | Bounded tone-pattern player on TIM4_CH1 |
| `App/Inc/buttons.h`, `App/Src/buttons.c` | EXTI → debounced NEXT/PREV/REPEAT events |
| `App/Inc/plc_link.h`, `App/Src/plc_link.c` | W5500 bring-up, TCP connect, poll/timeout/reconnect state machine |
| `App/Src/usb_device_stub.c` | No-op `MX_USB_DEVICE_Init()` + `hpcd_USB_FS` for the MODBUS variant (Task 11) |

**Created — vendored library**
| Path | Responsibility |
|---|---|
| `Lib/w5500/CMakeLists.txt` | Static library target, mirroring `Lib/tmc2209/CMakeLists.txt` |
| `Lib/w5500/ioLibrary/` | Upstream `Ethernet/{wizchip_conf,socket}.{c,h}` + `Ethernet/W5500/w5500.{c,h}` |
| `Lib/w5500/w5500_stm32.h`, `Lib/w5500/w5500_stm32.c` | SPI2 burst callbacks, CS, reset, `cris` no-ops |

**Modified**
| Path | Change |
|---|---|
| `App/Inc/braille_disc.h`, `App/Src/braille_disc.c` | Expose the dot-bitmask primitive (Task 1) |
| `App/Src/motion_planner.c` | `blink_warning()` moves out to `fault_led.c` (Task 2) |
| `App/Src/app_main.c`, `App/Inc/app_main.h` | Cooperative tick, variant gating (Task 12) |
| `CMakeLists.txt` | `Lib/w5500` subdirectory (Task 9); variant option and USB source exclusion (Task 11) |
| `docs/*.md` | Two new guides + four updates (Task 13) |
| `Core/Src/main.c` | `App_init()` call gains two handles, inside `USER CODE BEGIN 2` (Task 12) |

**Task order rationale:** Tasks 1–6 are pure logic and complete host-testable
units, so every behaviour in spec §5–§7 is pinned down and verified before any
hardware is involved. Tasks 7–10 are the hardware ports, each ending in a bench
check that proves it on its own — the buzzer by ear, the buttons by press, the
SPI bus against `VERSIONR`. Task 11 changes the build shape, Task 12 is the
first moment the whole system runs, and Task 13 brings the guides back in line
with the firmware.

---

## Phase 1 — Host harness and pure logic (no hardware)

### Task 1: Host test harness and the braille dot primitive

Spec §4.2 (braille_disc change), §12 (harness). `braille_disc.c` already keeps a 6-dot bitmask as its single source of truth but never exposes it, so nothing can command "all six raised".

**Files:**
- Create: `tests/Makefile`, `tests/test_support.h`, `tests/stubs/stm32f1xx_hal.h`, `tests/test_braille_dots.c`
- Modify: `App/Inc/braille_disc.h`, `App/Src/braille_disc.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `void braille_render_dots(uint8_t dots)`, `void braille_render_char(uint8_t latin1_char)`, `uint8_t braille_pattern_for_char(uint8_t c)`, `BRAILLE_DOTS_ALL_FLAT` (`0x00u`), `BRAILLE_DOTS_ALL_RAISED` (`0x3Fu`). `translate_char_on_disc(uint8_t byte)` keeps its exact current signature and behaviour.

- [ ] **Step 1: Create the HAL stub**

`App/Inc/motion_planner.h` includes `stm32f1xx_hal.h` for two handle types only. Create `tests/stubs/stm32f1xx_hal.h`:

```c
/* Minimal stand-in for the ST HAL header so App modules compile on the host.
 * App/Inc/motion_planner.h needs only these two handle typedefs; nothing in a
 * host test ever dereferences them. Add to this file only what a test
 * genuinely needs -- it is a seam, not a HAL reimplementation. */
#ifndef TESTS_STUB_STM32F1XX_HAL_H
#define TESTS_STUB_STM32F1XX_HAL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct { int unused; } UART_HandleTypeDef;
typedef struct { int unused; } TIM_HandleTypeDef;

#endif /* TESTS_STUB_STM32F1XX_HAL_H */
```

- [ ] **Step 2: Create the assert helpers**

Create `tests/test_support.h`:

```c
/* Tiny assertion helpers for the host suites. Each suite is one executable
 * that prints a line per check and exits non-zero if any failed, so `make`
 * fails loudly and the output says which expectation broke. */
#ifndef TESTS_TEST_SUPPORT_H
#define TESTS_TEST_SUPPORT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_failed;
static int tests_run;

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++tests_run;                                                               \
    if (!(cond)) {                                                             \
      ++tests_failed;                                                          \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                   \
    }                                                                          \
  } while (0)

#define CHECK_EQ(actual, expected)                                             \
  do {                                                                         \
    ++tests_run;                                                               \
    long a_ = (long)(actual), e_ = (long)(expected);                           \
    if (a_ != e_) {                                                            \
      ++tests_failed;                                                          \
      printf("FAIL %s:%d  %s == %ld, expected %ld\n", __FILE__, __LINE__,      \
             #actual, a_, e_);                                                 \
    }                                                                          \
  } while (0)

#define CHECK_STR_EQ(actual, expected)                                         \
  do {                                                                         \
    ++tests_run;                                                               \
    if (strcmp((actual), (expected)) != 0) {                                   \
      ++tests_failed;                                                          \
      printf("FAIL %s:%d  %s ==\n  \"%s\"\nexpected\n  \"%s\"\n", __FILE__,    \
             __LINE__, #actual, (actual), (expected));                         \
    }                                                                          \
  } while (0)

#define TESTS_REPORT(suite)                                                    \
  do {                                                                         \
    printf("%-20s %d checks, %d failed\n", (suite), tests_run, tests_failed);  \
    return tests_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;                    \
  } while (0)

#endif /* TESTS_TEST_SUPPORT_H */
```

- [ ] **Step 3: Create the Makefile**

Create `tests/Makefile`. It compiles each suite against `App/Inc` plus the stub directory. Suites include the module `.c` directly so `static` functions are reachable, which is why no App source is listed as a separate object.

```make
# Host-side unit tests. These build with the system gcc, not the ARM
# toolchain: every module under test is either pure logic or reached through a
# function-pointer seam, so no HAL is involved. Suites #include the module .c
# directly to reach its static functions.

CC      ?= gcc
CFLAGS  := -std=c11 -Wall -Wextra -Werror -g -O0 \
           -I../App/Inc -Istubs -I.
SUITES  := test_braille_dots

BINDIR  := build
BINS    := $(addprefix $(BINDIR)/,$(SUITES))

.PHONY: all test clean
all: test

test: $(BINS)
	@echo "== running host suites =="
	@fail=0; for b in $(BINS); do ./$$b || fail=1; done; exit $$fail

$(BINDIR)/%: %.c $(wildcard ../App/Src/*.c) $(wildcard ../App/Inc/*.h) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BINDIR):
	@mkdir -p $(BINDIR)

clean:
	@rm -rf $(BINDIR)
```

Add `tests/build` to `.gitignore` (append a line; the file already ignores `build`, but that pattern is anchored differently enough to be worth the explicit entry).

- [ ] **Step 4: Write the failing test**

Create `tests/test_braille_dots.c`. The expected angles come from the macros in `braille_disc.h`: disc 1 is `(weight * 225) % 900` with dot 1 → 1 and dot 4 → 2; disc 2 is `weight * 225` with dots 2,3,5,6 → 1,2,4,8.

```c
/* Task 1 -- the dot-bitmask primitive, plus a regression guard that the
 * existing character path still produces the angles it did before the
 * refactor. The all-flat/all-raised pair is what the reader uses for a
 * false/true value, so those two angles are the load-bearing cases. */
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>

/* move_to_angle() lives in motion_planner.c, which needs the real HAL. The
 * test supplies it instead and records what the module asked for. */
#include "motion_planner.h"

static disk_angles_t last_move;
static int move_count;

void move_to_angle(disk_angles_t disk_angle) {
  last_move = disk_angle;
  ++move_count;
}
void initialize_motors(UART_HandleTypeDef *a, UART_HandleTypeDef *b,
                       TIM_HandleTypeDef *c, TIM_HandleTypeDef *d) {
  (void)a; (void)b; (void)c; (void)d;
}
bool calibrate_zero_position(void) { return true; }

#include "braille_disc.c"

static void expect_move(uint16_t single, uint16_t doubl) {
  CHECK_EQ(last_move.single_row_disc, single);
  CHECK_EQ(last_move.double_row_disc, doubl);
}

int main(void) {
  /* All flat is the FALSE glyph: both discs at zero. */
  braille_render_dots(BRAILLE_DOTS_ALL_FLAT);
  expect_move(0u, 0u);

  /* All raised is the TRUE glyph. disc1 weight 3 -> (3*225)%900 = 675;
   * disc2 weight 15 -> 15*225 = 3375. */
  braille_render_dots(BRAILLE_DOTS_ALL_RAISED);
  expect_move(675u, 3375u);

  /* Every label letter used by the packet contract (spec section 5). */
  braille_render_char('A'); expect_move(225u, 0u);
  braille_render_char('B'); expect_move(225u, 225u);
  braille_render_char('E'); expect_move(225u, 900u);
  braille_render_char('S'); expect_move(450u, 675u);
  braille_render_char('P'); expect_move(675u, 675u);
  braille_render_char('T'); expect_move(450u, 1575u);
  braille_render_char('V'); expect_move(225u, 2475u);
  braille_render_char('R'); expect_move(225u, 1575u);
  braille_render_char('M'); expect_move(675u, 450u);
  braille_render_char('L'); expect_move(225u, 675u);

  /* Lowercase folds to uppercase, as before the refactor. */
  braille_render_char('a'); expect_move(225u, 0u);

  /* The pattern query the packet test relies on. */
  CHECK_EQ(braille_pattern_for_char('A'), 0x01u);
  CHECK_EQ(braille_pattern_for_char('L'), 0x07u);
  CHECK_EQ(braille_pattern_for_char('\0'), 0x00u);

  /* Regression: the UTF-8 stream entry point still behaves as documented.
   * 'a' renders immediately; a lone lead byte renders nothing. */
  move_count = 0;
  translate_char_on_disc('a');
  CHECK_EQ(move_count, 1);
  expect_move(225u, 0u);
  move_count = 0;
  translate_char_on_disc(0xC3u); /* lead byte of a 2-byte sequence */
  CHECK_EQ(move_count, 0);
  translate_char_on_disc(0xA7u); /* c-cedilla completes: dots 1,2,3,4,6 */
  CHECK_EQ(move_count, 1);

  TESTS_REPORT("braille_dots");
}
```

- [ ] **Step 5: Run the test to verify it fails**

```bash
make -C tests
```

Expected: compile failure — `braille_render_dots`, `braille_render_char`, `braille_pattern_for_char`, `BRAILLE_DOTS_ALL_FLAT` and `BRAILLE_DOTS_ALL_RAISED` are all undeclared.

- [ ] **Step 6: Extend `braille_disc.h`**

Add to `App/Inc/braille_disc.h`, inside the existing `@defgroup braille_disc` block and above the `translate_char_on_disc()` declaration:

```c
/**
 * @name Value glyphs
 *
 * The two dot patterns the PLC reader uses for a boolean: no dots raised for
 * false, all six raised for true. They are deliberately not letters, so a
 * reader feeling a cell can always tell a value from a label glyph.
 * @{
 */
#define BRAILLE_DOTS_ALL_FLAT 0x00u   /**< No dot raised: blank cell. */
#define BRAILLE_DOTS_ALL_RAISED 0x3Fu /**< Dots 1-6 raised.           */
/** @} */

/**
 * @brief Render an explicit 6-dot pattern on the cell and block until the
 *        discs arrive.
 *
 * The dot bits follow the canonical Unicode Braille Patterns numbering:
 * bit 0 is dot 1, bit 1 dot 2, ... bit 5 dot 6. Bits above 5 are ignored.
 *
 * @param dots 6-dot bitmask, e.g. @ref BRAILLE_DOTS_ALL_RAISED.
 */
void braille_render_dots(uint8_t dots);

/**
 * @brief Render one Latin-1 character on the cell (case-insensitive).
 *
 * Use this for characters the firmware itself chooses, such as the packet's
 * label letters. Input arriving as a UTF-8 byte stream from a host must go
 * through @ref translate_char_on_disc instead.
 *
 * @param latin1_char Character code 0..255; unmapped codes render blank.
 */
void braille_render_char(uint8_t latin1_char);

/**
 * @brief Look up the 6-dot pattern for a Latin-1 character without moving.
 *
 * @param c Character code 0..255 (case-insensitive).
 * @return 6-dot bitmask; 0 for any unmapped code.
 */
uint8_t braille_pattern_for_char(uint8_t c);
```

- [ ] **Step 7: Refactor `braille_disc.c`**

Three edits, no behaviour change to the existing path:

1. Rename `static uint8_t pattern_for_char(uint8_t c)` to `uint8_t braille_pattern_for_char(uint8_t c)` (drop `static`), keeping its body and Doxygen comment. Update its two callers inside the weight helpers.
2. Change the two weight helpers to take a pattern instead of a character, since both already start by looking one up and the new entry points have the pattern in hand:

```c
/**
 * @brief Get the weight for the single-row disc (top row: dots 1,4).
 * @param pattern 6-dot bitmask.
 * @return Weight in 0..3 (dot 1 -> 1, dot 4 -> 2).
 */
static uint8_t get_character_weight_single_row_disc(uint8_t pattern) {
  uint8_t weight = 0u;
  if (pattern & DOT1) {
    weight |= 1u; /* dot 1 -> value 1 */
  }
  if (pattern & DOT4) {
    weight |= 2u; /* dot 4 -> value 2 */
  }
  return weight;
}

/**
 * @brief Get the weight for the double-row disc (dots 2,3,5,6).
 * @param pattern 6-dot bitmask.
 * @return Weight in 0..15 ([d6 d5 d3 d2], d6 most significant).
 */
static uint8_t get_character_weight_double_row_disc(uint8_t pattern) {
  uint8_t weight = 0u;
  if (pattern & DOT2) {
    weight |= W2_DOT2;
  }
  if (pattern & DOT3) {
    weight |= W2_DOT3;
  }
  if (pattern & DOT5) {
    weight |= W2_DOT5;
  }
  if (pattern & DOT6) {
    weight |= W2_DOT6;
  }
  return weight;
}
```

3. Delete the `angle_single_row_disc`/`angle_double_row_disc` wrappers and replace `translate_char_on_disc()` with the three-function tail below. The dot mask is now the seam: everything funnels through `braille_render_dots()`.

```c
void braille_render_dots(uint8_t dots) {
  disk_angles_t braille_cell = {0, 0};

  uint8_t weight_single = get_character_weight_single_row_disc(dots);
  uint8_t weight_double = get_character_weight_double_row_disc(dots);

  braille_cell.single_row_disc = DISC1_ANGLE_TO_POS_ANG(weight_single);
  braille_cell.double_row_disc = DISC2_ANGLE_TO_POS_ANG(weight_double);

  move_to_angle(braille_cell);
}

void braille_render_char(uint8_t latin1_char) {
  braille_render_dots(braille_pattern_for_char(latin1_char));
}

void translate_char_on_disc(uint8_t byte) {
  uint32_t codepoint = utf8_decode_byte(byte);
  if (codepoint == UTF8_INCOMPLETE) {
    return; // mid-sequence: no character to render yet, discs stay put
  }

  // Outside Latin-1 there is no cell for the character, so render a blank one
  // rather than aliasing it onto some other letter.
  braille_render_char((codepoint <= 0xFFu) ? (uint8_t)codepoint : 0u);
}
```

Also update the file's header comment: the "single source of truth" sentence should now say the bitmask is reachable through `braille_render_dots()`.

- [ ] **Step 8: Run the test to verify it passes**

```bash
make -C tests
```
Expected: `braille_dots  <N> checks, 0 failed` and exit 0.

- [ ] **Step 9: Confirm the firmware still builds and measure**

```bash
export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
cmake --build --preset Debug && cmake --build --preset Release
arm-none-eabi-size build/Debug/single-cell-braille-controller.elf
arm-none-eabi-size build/Release/single-cell-braille-controller.elf
```
Expected: both link. Release stays within a few dozen bytes of 21 688 text — this task adds no new behaviour to the firmware image.

- [ ] **Step 10: Commit**

```bash
git add tests .gitignore App/Inc/braille_disc.h App/Src/braille_disc.c
git commit -m "feat: expose the braille dot-bitmask primitive, add host test harness

The 6-dot mask was already braille_disc.c's single source of truth but was
unreachable, so nothing could command the all-flat/all-raised pair the PLC
reader needs for a boolean value. braille_render_dots() makes the mask the
seam that every render funnels through; braille_render_char() sits on top for
characters the firmware picks itself, and translate_char_on_disc() keeps its
signature and behaviour as the UTF-8 stream entry point for the USB variant.

Also formalises the throwaway host-stub technique from earlier sessions into a
committed tests/ harness, so the pure modules that follow have somewhere to be
tested. The suite includes braille_disc.c directly to reach its statics and
supplies move_to_angle() as a recording stub.

Size unchanged (Release text 21688 B).

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Extract `fault_led`

Spec §4.2. `blink_warning()` is `static` in `motion_planner.c:107`; `plc_link` needs it too, so it gets one home rather than a second copy.

**Files:**
- Create: `App/Inc/fault_led.h`, `App/Src/fault_led.c`
- Modify: `App/Src/motion_planner.c`

**Interfaces:**
- Consumes: nothing.
- Produces: `void fault_led_blink(uint32_t blinks)`, `FAULT_LED_CODE_MOTOR_01` (`1u`), `FAULT_LED_CODE_MOTOR_02` (`2u`), `FAULT_LED_CODE_ETHERNET` (`3u`).

- [ ] **Step 1: Create the header**

```c
/**
 * @file    fault_led.h
 * @brief   Startup fault reporting on the board LED.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * A subsystem that fails during bring-up reports its identity as a blink
 * count rather than halting, so a board with one broken peripheral still
 * starts and can be diagnosed. Codes are allocated here so two subsystems
 * cannot silently pick the same one.
 */

#ifndef FAULT_LED_H
#define FAULT_LED_H

#include <stdint.h>

/**
 * @defgroup fault_led Fault LED
 * @brief    Blink-code fault reporting shared by every subsystem.
 * @{
 */

/**
 * @name Blink codes
 *
 * One per subsystem that can fail at bring-up. Reading these apart by eye is
 * only possible while they stay small and distinct.
 * @{
 */
#define FAULT_LED_CODE_MOTOR_01 1u /**< Motor 01 driver or ZERO sensor. */
#define FAULT_LED_CODE_MOTOR_02 2u /**< Motor 02 driver or ZERO sensor. */
#define FAULT_LED_CODE_ETHERNET 3u /**< W5500 absent or not answering.  */
/** @} */

/**
 * @brief Report a fault without halting: @p blinks short blinks, a long
 *        pause, three times over, then return.
 *
 * Blocking, and deliberately so: every caller is on the startup path, where
 * nothing else needs to run. Do not call it from the main loop.
 *
 * @param blinks One of the @c FAULT_LED_CODE_* values.
 */
void fault_led_blink(uint32_t blinks);

/** @} */ // end of fault_led

#endif // FAULT_LED_H
```

- [ ] **Step 2: Create the implementation**

Move the body verbatim from `motion_planner.c` — including the `GPIO_PIN_SET`-first order, which is correct for the active-low LED on PC13.

```c
// ----------------------------------------------------------------------------
// fault_led.c
//
// The blink-code reporter shared by every subsystem that can fail during
// bring-up. It lived in motion_planner.c while the motors were the only
// callers; the Ethernet link is the third, so it moved here instead of being
// copied.
// ----------------------------------------------------------------------------

#include "fault_led.h"
#include "main.h"
#include "stm32f1xx_hal.h"

// Shape of a blink code: <code> short blinks, a long gap, repeated.
#define FAULT_LED_BURSTS 3u
#define FAULT_LED_ON_MS 100u
#define FAULT_LED_OFF_MS 200u
#define FAULT_LED_GAP_MS 800u

void fault_led_blink(uint32_t blinks) {
  for (uint32_t burst = 0u; burst < FAULT_LED_BURSTS; ++burst) {
    for (uint32_t i = 0u; i < blinks; ++i) {
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
      HAL_Delay(FAULT_LED_ON_MS);
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
      HAL_Delay(FAULT_LED_OFF_MS);
    }
    HAL_Delay(FAULT_LED_GAP_MS);
  }
}
```

- [ ] **Step 3: Update `motion_planner.c`**

- Add `#include "fault_led.h"`.
- Delete the `static void blink_warning(uint32_t blinks)` function and its comment.
- Delete `APP_MOTOR_01_BLINK_CODE`, `APP_MOTOR_02_BLINK_CODE`, `APP_WARNING_BURSTS`, `APP_WARNING_ON_MS`, `APP_WARNING_OFF_MS`, `APP_WARNING_GAP_MS`.
- Replace all four call sites (`motion_planner.c:160`, `:164`, `:339`, `:346`): `blink_warning(APP_MOTOR_01_BLINK_CODE)` → `fault_led_blink(FAULT_LED_CODE_MOTOR_01)`, and likewise for motor 02.

- [ ] **Step 4: Verify no stale references remain**

```bash
grep -rn "blink_warning\|APP_WARNING_\|APP_MOTOR_0._BLINK_CODE" App/ || echo "clean"
```
Expected: `clean`.

- [ ] **Step 5: Build, run host tests, measure**

```bash
export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
cmake --build --preset Debug && cmake --build --preset Release
arm-none-eabi-size build/Release/single-cell-braille-controller.elf
make -C tests
```
Expected: both presets link, host suites pass, Release text within a few bytes of 21 688 (a pure move).

- [ ] **Step 6: Commit**

```bash
git add App/Inc/fault_led.h App/Src/fault_led.c App/Src/motion_planner.c
git commit -m "refactor: move blink_warning into a shared fault_led module

blink_warning() was static in motion_planner.c because the two motors were
its only callers. The Ethernet link is a third caller, so the function and
its blink-code constants move to fault_led.c rather than being duplicated.

Putting the codes in one header is the point: motor 01 is 1, motor 02 is 2,
Ethernet is 3, and a future subsystem cannot quietly reuse a number. docs/04
already notes that these patterns are only readable by eye while they stay
small and distinct.

Pure move, no behaviour change; the active-low LED write order is preserved
verbatim. Release text 21688 B.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: `plc_config.h`

Spec §9. Every tunable number in one place so no literal appears at a use site.

**Files:**
- Create: `App/Inc/plc_config.h`

**Interfaces:**
- Consumes: nothing.
- Produces: the macros below. `PLC_COIL_COUNT` is `11u`; `PLC_FIELD_COUNT` (Task 4) is static-asserted equal to it.

- [ ] **Step 1: Create the header**

```c
/**
 * @file    plc_config.h
 * @brief   Deployment settings for the MODBUS TCP link and the reader.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * Everything an installation might need to change lives here, so no numeric
 * literal appears at a use site. Editing this file and reflashing is the
 * supported way to move the device to another network or PLC; there is no
 * runtime configuration.
 */

#ifndef PLC_CONFIG_H
#define PLC_CONFIG_H

#include <stdint.h>

/**
 * @defgroup plc_config PLC Configuration
 * @brief    Network identity, PLC endpoint, packet range and timings.
 * @{
 */

/**
 * @name This device's network identity (static; there is no DHCP client)
 * @{
 */
#define PLC_DEVICE_MAC_0 0x00u
#define PLC_DEVICE_MAC_1 0x08u
#define PLC_DEVICE_MAC_2 0xDCu
#define PLC_DEVICE_MAC_3 0x11u
#define PLC_DEVICE_MAC_4 0x22u
#define PLC_DEVICE_MAC_5 0x33u

#define PLC_DEVICE_IP_0 10u
#define PLC_DEVICE_IP_1 0u
#define PLC_DEVICE_IP_2 0u
#define PLC_DEVICE_IP_3 50u

#define PLC_DEVICE_MASK_0 255u
#define PLC_DEVICE_MASK_1 255u
#define PLC_DEVICE_MASK_2 255u
#define PLC_DEVICE_MASK_3 0u

#define PLC_GATEWAY_0 10u
#define PLC_GATEWAY_1 0u
#define PLC_GATEWAY_2 0u
#define PLC_GATEWAY_3 1u
/** @} */

/**
 * @name The PLC endpoint and the package it serves
 * @{
 */
#define PLC_SERVER_IP_0 10u
#define PLC_SERVER_IP_1 0u
#define PLC_SERVER_IP_2 0u
#define PLC_SERVER_IP_3 204u

#define PLC_SERVER_PORT 503u  /**< Not the 502 default; this PLC uses 503. */
#define PLC_LOCAL_PORT 50000u /**< Our ephemeral source port.              */
#define PLC_UNIT_ID 1u        /**< MODBUS unit/slave id.                   */

#define PLC_COIL_BASE_ADDRESS 0u /**< First coil of the package.           */
#define PLC_COIL_COUNT 11u       /**< Coils in one package.                */
#define PLC_SOCKET_NUMBER 0u     /**< W5500 socket used for the link.      */
/** @} */

/**
 * @name Reader timings
 * @{
 */
/** How long a label glyph is held before the next glyph replaces it. Raising
 *  this makes a full 11-field read proportionally slower; at 1500 ms a read is
 *  roughly 45 s. */
#define PLC_LABEL_DWELL_MS 1500u
/** @} */

/**
 * @name Link timings
 * @{
 */
#define PLC_POLL_INTERVAL_MS 500u     /**< Gap between background polls.   */
#define PLC_RESPONSE_TIMEOUT_MS 1000u /**< Reply deadline for one request. */
#define PLC_RECONNECT_DELAY_MS 2000u  /**< Pause before retrying a link.   */
#define PLC_W5500_RESET_LOW_MS 2u     /**< RSTn hold; datasheet min 500us. */
#define PLC_W5500_BOOT_MS 10u         /**< PLL settle after RSTn release.  */
/** @} */

/**
 * @name Rate limits
 *
 * Without these, a chattering signal or an unplugged cable would make the
 * buzzer sound continuously and drown out the sounds that matter.
 * @{
 */
#define PLC_QUEUED_SOUND_MIN_INTERVAL_MS 5000u
#define PLC_FAULT_SOUND_MIN_INTERVAL_MS 10000u
/** @} */

/** @brief Debounce lockout: edges within this window are one press. */
#define PLC_BUTTON_DEBOUNCE_LOCKOUT_MS 40u

/** @} */ // end of plc_config

#endif // PLC_CONFIG_H
```

- [ ] **Step 2: Verify it compiles standalone**

```bash
gcc -std=c11 -Wall -Wextra -Werror -fsyntax-only -x c App/Inc/plc_config.h && echo "OK"
```
Expected: `OK`.

- [ ] **Step 3: Commit**

```bash
git add App/Inc/plc_config.h
git commit -m "feat: add plc_config.h with the deployment settings

One home for every number an installation might change: this device's static
identity, the PLC endpoint (port 503, not the 502 default), the coil range,
and the reader and link timings. Nothing here is discovered at runtime -- the
design chose static configuration over DHCP so there is no boot path that can
stall waiting for a server that never answers.

Splitting the addresses into per-octet macros rather than strings keeps them
usable directly as the uint8_t[4] arrays ioLibrary's wiz_NetInfo expects, with
no parsing on the device.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: `plc_packet` — the packet contract

Spec §5. The 11-field table, its labels, and the snapshot type. Pure data plus three accessors; no hardware, fully host-tested.

**Files:**
- Create: `App/Inc/plc_packet.h`, `App/Src/plc_packet.c`, `tests/test_plc_packet.c`
- Modify: `tests/Makefile` (add the suite)

**Interfaces:**
- Consumes: `PLC_COIL_COUNT`, `PLC_COIL_BASE_ADDRESS` (Task 3); `braille_pattern_for_char()` (Task 1, test only).
- Produces: `plc_snapshot_t` (a struct wrapping `uint16_t bits`), `plc_field_t`, `PLC_FIELD_COUNT` (`11u`), `PLC_LABEL_MAX_GLYPHS` (`2u`), `const plc_field_t *plc_packet_field(uint8_t index)`, `bool plc_snapshot_bit(plc_snapshot_t, uint8_t index)`, `bool plc_snapshot_equal(plc_snapshot_t, plc_snapshot_t)`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_plc_packet.c`. The last three checks are the important ones: they defend the property that makes mixed-length labels safe (spec §5).

```c
/* Task 4 -- the packet contract. Most of this suite guards invariants rather
 * than behaviour, because the table is data an editor will come back to: the
 * coil range must match what the MODBUS request asks for, and no label glyph
 * may collide with a value glyph. */
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>

#include "motion_planner.h"

/* braille_disc.c is included for braille_pattern_for_char(); it needs
 * move_to_angle(), which no check here triggers. */
void move_to_angle(disk_angles_t disk_angle) { (void)disk_angle; }
void initialize_motors(UART_HandleTypeDef *a, UART_HandleTypeDef *b,
                       TIM_HandleTypeDef *c, TIM_HandleTypeDef *d) {
  (void)a; (void)b; (void)c; (void)d;
}
bool calibrate_zero_position(void) { return true; }

#include "braille_disc.c"
#include "plc_packet.c"

int main(void) {
  /* The table and the MODBUS request must agree about the package size. */
  CHECK_EQ(PLC_FIELD_COUNT, PLC_COIL_COUNT);
  CHECK_EQ(PLC_FIELD_COUNT, 11u);

  /* Out-of-range lookups are rejected, not clamped. */
  CHECK(plc_packet_field(0u) != NULL);
  CHECK(plc_packet_field((uint8_t)(PLC_FIELD_COUNT - 1u)) != NULL);
  CHECK(plc_packet_field((uint8_t)PLC_FIELD_COUNT) == NULL);

  /* Coils are unique, ascending, and inside the range we request. */
  for (uint8_t i = 0u; i < PLC_FIELD_COUNT; ++i) {
    const plc_field_t *f = plc_packet_field(i);
    CHECK(f->coil >= PLC_COIL_BASE_ADDRESS);
    CHECK(f->coil < PLC_COIL_BASE_ADDRESS + PLC_COIL_COUNT);
    if (i > 0u) {
      CHECK(f->coil > plc_packet_field((uint8_t)(i - 1u))->coil);
    }
  }

  /* Labels are 1..2 glyphs and match the design's table. */
  const char *expected[PLC_FIELD_COUNT] = {"AE", "AS", "BE", "BS", "P",
                                           "S",  "T",  "V",  "R",  "M", "L"};
  for (uint8_t i = 0u; i < PLC_FIELD_COUNT; ++i) {
    const plc_field_t *f = plc_packet_field(i);
    CHECK(f->glyph_count >= 1u);
    CHECK(f->glyph_count <= PLC_LABEL_MAX_GLYPHS);
    char got[PLC_LABEL_MAX_GLYPHS + 1u];
    for (uint8_t g = 0u; g < f->glyph_count; ++g) {
      got[g] = (char)f->label[g];
    }
    got[f->glyph_count] = '\0';
    CHECK_STR_EQ(got, expected[i]);
  }

  /* THE invariant that makes variable-length labels readable: every label
   * glyph is a real letter, and none of them is all-flat or all-raised, so a
   * value glyph can never be mistaken for a label glyph still to come.
   * Adding a label letter such as E-acute (all six dots) would break this. */
  for (uint8_t i = 0u; i < PLC_FIELD_COUNT; ++i) {
    const plc_field_t *f = plc_packet_field(i);
    for (uint8_t g = 0u; g < f->glyph_count; ++g) {
      uint8_t dots = braille_pattern_for_char(f->label[g]);
      CHECK(dots != BRAILLE_DOTS_ALL_FLAT);
      CHECK(dots != BRAILLE_DOTS_ALL_RAISED);
    }
  }

  /* Snapshot accessors: bit position follows the field's coil offset, so
   * display order and coil order are independent. */
  plc_snapshot_t none = {0x0000u};
  plc_snapshot_t all = {0x07FFu}; /* 11 bits set */
  plc_snapshot_t first_only = {0x0001u};
  plc_snapshot_t last_only = {0x0400u}; /* bit 10 */

  for (uint8_t i = 0u; i < PLC_FIELD_COUNT; ++i) {
    CHECK(plc_snapshot_bit(none, i) == false);
    CHECK(plc_snapshot_bit(all, i) == true);
  }
  CHECK(plc_snapshot_bit(first_only, 0u) == true);
  CHECK(plc_snapshot_bit(first_only, 1u) == false);
  CHECK(plc_snapshot_bit(last_only, 10u) == true);
  CHECK(plc_snapshot_bit(last_only, 9u) == false);

  CHECK(plc_snapshot_equal(none, none) == true);
  CHECK(plc_snapshot_equal(none, all) == false);
  CHECK(plc_snapshot_equal(all, all) == true);

  TESTS_REPORT("plc_packet");
}
```

- [ ] **Step 2: Add the suite to the Makefile**

In `tests/Makefile`, change:
```make
SUITES  := test_braille_dots
```
to:
```make
SUITES  := test_braille_dots test_plc_packet
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
make -C tests
```
Expected: `test_plc_packet.c` fails to compile — `plc_packet.c` does not exist.

- [ ] **Step 4: Create the header**

```c
/**
 * @file    plc_packet.h
 * @brief   The fixed package of PLC signals and how it is labelled.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * The PLC always serves the same package: a run of coils whose meaning is
 * fixed at build time. This module is that contract -- which coil carries
 * which signal, and the braille letters that name it -- plus the snapshot
 * type the rest of the firmware passes around.
 *
 * Labels are one or two letters. That is safe only because a value renders as
 * all-flat or all-raised and no label letter is either, so the value always
 * announces itself as the end of the sequence. Never add a label glyph whose
 * pattern is @ref BRAILLE_DOTS_ALL_FLAT or @ref BRAILLE_DOTS_ALL_RAISED.
 */

#ifndef PLC_PACKET_H
#define PLC_PACKET_H

#include <stdbool.h>
#include <stdint.h>

#include "plc_config.h"

/**
 * @defgroup plc_packet PLC Packet
 * @brief    The fixed signal package and its braille labels.
 * @{
 */

/** @brief Fields in one package; static-asserted equal to PLC_COIL_COUNT. */
#define PLC_FIELD_COUNT 11u

/** @brief Longest label, in glyphs. Each glyph costs one disc move. */
#define PLC_LABEL_MAX_GLYPHS 2u

/**
 * @brief One package's worth of coil states.
 *
 * Wrapped in a struct so it cannot be confused with a plain integer and so
 * the accessors are the only way in. One bit per coil, bit @c n being the
 * coil at @c PLC_COIL_BASE_ADDRESS+n.
 */
typedef struct {
  uint16_t bits; /**< Coil states, LSB = first coil of the package. */
} plc_snapshot_t;

/** @brief One monitored signal: where to read it and what to call it. */
typedef struct {
  uint8_t coil;        /**< Absolute coil address.                        */
  uint8_t glyph_count; /**< Label length, 1..PLC_LABEL_MAX_GLYPHS.        */
  uint8_t label[PLC_LABEL_MAX_GLYPHS]; /**< Label letters, ASCII.         */
} plc_field_t;

/**
 * @brief Look up one field of the package.
 *
 * @param index Field index in reading order, 0..PLC_FIELD_COUNT-1.
 * @return The field, or NULL when @p index is out of range.
 */
const plc_field_t *plc_packet_field(uint8_t index);

/**
 * @brief Read one field's boolean state out of a snapshot.
 *
 * The bit is selected by the field's coil address, not by @p index, so the
 * order fields are read aloud in is independent of their coil order.
 *
 * @param snapshot Snapshot to read.
 * @param index    Field index, 0..PLC_FIELD_COUNT-1.
 * @return The signal's state; false for an out-of-range index.
 */
bool plc_snapshot_bit(plc_snapshot_t snapshot, uint8_t index);

/**
 * @brief Compare two snapshots.
 *
 * This is how a background poll decides whether anything actually changed,
 * so it must stay a whole-package comparison.
 *
 * @param a First snapshot.
 * @param b Second snapshot.
 * @return true when every coil matches.
 */
bool plc_snapshot_equal(plc_snapshot_t a, plc_snapshot_t b);

/** @} */ // end of plc_packet

#endif // PLC_PACKET_H
```

- [ ] **Step 5: Create the implementation**

```c
// ----------------------------------------------------------------------------
// plc_packet.c
//
// The package contract. The table below is the single place that knows what
// the PLC's coils mean; everything else works in field indices.
//
// Label letters are Portuguese mnemonics: E is "entrada" and S is "saida" for
// the SMEMA handshake directions, R is "veRmelho" and M is "aMarelo" so the
// three signal-lamp colours stay distinct, and L is "liberado" for released.
// ----------------------------------------------------------------------------

#include "plc_packet.h"

#include "plc_config.h"

_Static_assert(PLC_FIELD_COUNT == PLC_COIL_COUNT,
               "the label table and the MODBUS request disagree about the "
               "package size");
_Static_assert(PLC_COIL_COUNT <= 16u,
               "plc_snapshot_t holds the package in a uint16_t");
_Static_assert(PLC_LABEL_MAX_GLYPHS == 2u,
               "the table below writes at most two label glyphs per row");

// Reading order, which is also coil order here. Cost: 44 B flash, 0 B RAM.
static const plc_field_t plc_fields[PLC_FIELD_COUNT] = {
    {0u, 2u, {'A', 'E'}},   // SMEMA A IN
    {1u, 2u, {'A', 'S'}},   // SMEMA A OUT
    {2u, 2u, {'B', 'E'}},   // SMEMA B IN
    {3u, 2u, {'B', 'S'}},   // SMEMA B OUT
    {4u, 1u, {'P', 0u}},    // SENSOR PRESENCA
    {5u, 1u, {'S', 0u}},    // STOP_LINE
    {6u, 1u, {'T', 0u}},    // SEND_TIME
    {7u, 1u, {'V', 0u}},    // VERDE
    {8u, 1u, {'R', 0u}},    // VERMELHO
    {9u, 1u, {'M', 0u}},    // AMARELO
    {10u, 1u, {'L', 0u}},   // released
};

const plc_field_t *plc_packet_field(uint8_t index) {
  if (index >= PLC_FIELD_COUNT) {
    return NULL;
  }
  return &plc_fields[index];
}

bool plc_snapshot_bit(plc_snapshot_t snapshot, uint8_t index) {
  const plc_field_t *field = plc_packet_field(index);
  if (field == NULL) {
    return false;
  }
  uint8_t bit = (uint8_t)(field->coil - PLC_COIL_BASE_ADDRESS);
  return ((snapshot.bits >> bit) & 1u) != 0u;
}

bool plc_snapshot_equal(plc_snapshot_t a, plc_snapshot_t b) {
  return a.bits == b.bits;
}
```

`NULL` needs `<stddef.h>`; it arrives via `<stdint.h>` on this toolchain, but add `#include <stddef.h>` to the header explicitly rather than relying on that.

- [ ] **Step 6: Run the test to verify it passes**

```bash
make -C tests
```
Expected: both suites report `0 failed`.

- [ ] **Step 7: Build the firmware and measure**

```bash
export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
cmake --build --preset Release && arm-none-eabi-size build/Release/single-cell-braille-controller.elf
```
Expected: links. Nothing calls into `plc_packet` yet, so `--gc-sections` drops it and text barely moves.

- [ ] **Step 8: Commit**

```bash
git add App/Inc/plc_packet.h App/Src/plc_packet.c tests/test_plc_packet.c tests/Makefile
git commit -m "feat: add the PLC packet contract and snapshot type

One table holds what the PLC's coils mean; the rest of the firmware works in
field indices and never sees a coil address. Snapshots are a uint16_t in a
struct, so change detection between two polls is a single integer compare and
the value cannot be mistaken for a bare integer at a call site.

plc_snapshot_bit() selects its bit from the field's coil address rather than
from the field index, which keeps reading order independent of coil order --
the table can be reordered for a better reading sequence without touching the
MODBUS layer.

Three static assertions tie the table to the request: field count equals coil
count, the package fits a uint16_t, and the table's row shape matches
PLC_LABEL_MAX_GLYPHS. The test additionally guards the invariant the design
depends on -- no label glyph is all-flat or all-raised, so a value glyph
always reads as the end of a sequence.

Release text unchanged (not yet referenced).

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: `modbus_tcp` — the PDU codec

Spec §8. Pure encode/decode with no sockets, so every malformed-frame path is testable on the host.

**Files:**
- Create: `App/Inc/modbus_tcp.h`, `App/Src/modbus_tcp.c`, `tests/test_modbus_tcp.c`
- Modify: `tests/Makefile`

**Interfaces:**
- Consumes: nothing.
- Produces: `modbus_status_t` (enum below), `MODBUS_MBAP_LEN` (`7u`), `MODBUS_FC_READ_COILS` (`0x01u`), `MODBUS_MAX_COILS` (`16u`), `MODBUS_READ_COILS_REQ_LEN` (`12u`), `MODBUS_COIL_BYTES(n)`, `MODBUS_READ_COILS_RSP_LEN(n)`, `uint16_t modbus_build_read_coils(uint8_t *out, uint16_t out_capacity, uint16_t transaction_id, uint8_t unit_id, uint16_t start_address, uint16_t quantity)` returning bytes written or `0`, and `modbus_status_t modbus_parse_read_coils(const uint8_t *frame, uint16_t length, uint16_t transaction_id, uint8_t unit_id, uint16_t quantity, uint16_t *out_bits, uint8_t *out_exception_code)`.

- [ ] **Step 1: Write the failing test**

```c
/* Task 5 -- the MODBUS TCP codec. The parser's job is to be suspicious: a
 * response that decodes cleanly but belongs to an earlier request would show
 * the reader stale values with no sign anything was wrong, so the stale
 * transaction-id case matters as much as the malformed ones. */
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "modbus_tcp.c"

#define TID 0x1234u
#define UNIT 1u
#define ADDR 0u
#define QTY 11u

/* A well-formed 11-coil response carrying the given two data bytes. */
static uint16_t make_response(uint8_t *f, uint16_t tid, uint8_t unit,
                              uint8_t fc, uint8_t byte_count, uint8_t d0,
                              uint8_t d1) {
  f[0] = (uint8_t)(tid >> 8);
  f[1] = (uint8_t)(tid & 0xFFu);
  f[2] = 0u;
  f[3] = 0u;
  f[4] = 0u;
  f[5] = (uint8_t)(3u + byte_count); /* unit + fc + count + data */
  f[6] = unit;
  f[7] = fc;
  f[8] = byte_count;
  f[9] = d0;
  f[10] = d1;
  return (uint16_t)(9u + byte_count);
}

int main(void) {
  uint8_t req[MODBUS_READ_COILS_REQ_LEN];
  uint16_t bits = 0xFFFFu;
  uint8_t exc = 0xFFu;

  /* ---- build ---- */
  CHECK_EQ(modbus_build_read_coils(req, sizeof req, TID, UNIT, ADDR, QTY),
           MODBUS_READ_COILS_REQ_LEN);
  CHECK_EQ(req[0], 0x12u);              /* transaction id hi */
  CHECK_EQ(req[1], 0x34u);              /* transaction id lo */
  CHECK_EQ(req[2], 0x00u);              /* protocol id hi    */
  CHECK_EQ(req[3], 0x00u);              /* protocol id lo    */
  CHECK_EQ(req[4], 0x00u);              /* length hi         */
  CHECK_EQ(req[5], 0x06u);              /* length lo         */
  CHECK_EQ(req[6], UNIT);               /* unit id           */
  CHECK_EQ(req[7], MODBUS_FC_READ_COILS);
  CHECK_EQ(req[8], 0x00u);              /* start address hi  */
  CHECK_EQ(req[9], 0x00u);              /* start address lo  */
  CHECK_EQ(req[10], 0x00u);             /* quantity hi       */
  CHECK_EQ(req[11], 0x0Bu);             /* quantity lo = 11  */

  /* Too small a buffer is refused rather than truncated. */
  CHECK_EQ(modbus_build_read_coils(req, MODBUS_READ_COILS_REQ_LEN - 1u, TID,
                                   UNIT, ADDR, QTY),
           0u);
  /* Quantities the uint16_t snapshot cannot hold are refused. */
  CHECK_EQ(modbus_build_read_coils(req, sizeof req, TID, UNIT, ADDR,
                                   MODBUS_MAX_COILS + 1u),
           0u);
  CHECK_EQ(modbus_build_read_coils(req, sizeof req, TID, UNIT, ADDR, 0u), 0u);

  /* ---- parse: the happy path ---- */
  uint8_t f[32];
  uint16_t n = make_response(f, TID, UNIT, MODBUS_FC_READ_COILS, 2u, 0x15u,
                             0x05u);
  CHECK_EQ(n, MODBUS_READ_COILS_RSP_LEN(QTY));
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_OK);
  /* 0x15 = coils 0,2,4 ; 0x05 = coils 8,10 */
  CHECK_EQ(bits, 0x0515u);

  /* Every coil individually, to prove the bit order. */
  for (uint8_t i = 0u; i < QTY; ++i) {
    uint8_t d0 = (i < 8u) ? (uint8_t)(1u << i) : 0u;
    uint8_t d1 = (i >= 8u) ? (uint8_t)(1u << (i - 8u)) : 0u;
    n = make_response(f, TID, UNIT, MODBUS_FC_READ_COILS, 2u, d0, d1);
    CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
             MODBUS_OK);
    CHECK_EQ(bits, (uint16_t)(1u << i));
  }

  /* Bits above the requested quantity are masked off, so a slave that pads
   * the last byte cannot inject phantom signals. */
  n = make_response(f, TID, UNIT, MODBUS_FC_READ_COILS, 2u, 0xFFu, 0xFFu);
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_OK);
  CHECK_EQ(bits, 0x07FFu);

  /* ---- parse: rejections ---- */
  n = make_response(f, TID, UNIT, MODBUS_FC_READ_COILS, 2u, 0u, 0u);

  CHECK_EQ(modbus_parse_read_coils(f, (uint16_t)(MODBUS_MBAP_LEN + 1u), TID,
                                   UNIT, QTY, &bits, &exc),
           MODBUS_ERR_TOO_SHORT);

  f[2] = 0x01u; /* non-zero protocol id */
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_PROTOCOL_ID);
  f[2] = 0x00u;

  CHECK_EQ(modbus_parse_read_coils(f, n, (uint16_t)(TID + 1u), UNIT, QTY,
                                   &bits, &exc),
           MODBUS_ERR_TRANSACTION_ID);

  CHECK_EQ(modbus_parse_read_coils(f, n, TID, (uint8_t)(UNIT + 1u), QTY, &bits,
                                   &exc),
           MODBUS_ERR_UNIT_ID);

  f[5] = 0x09u; /* MBAP length field disagrees with the frame */
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_LENGTH_FIELD);
  f[5] = 0x05u;

  n = make_response(f, TID, UNIT, MODBUS_FC_READ_COILS, 3u, 0u, 0u);
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_BYTE_COUNT);

  n = make_response(f, TID, UNIT, 0x03u, 2u, 0u, 0u); /* Read Holding Regs */
  CHECK_EQ(modbus_parse_read_coils(f, n, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_FUNCTION);

  /* An exception response is 9 bytes: MBAP, FC|0x80, exception code. */
  f[0] = (uint8_t)(TID >> 8);
  f[1] = (uint8_t)(TID & 0xFFu);
  f[2] = 0u; f[3] = 0u; f[4] = 0u; f[5] = 0x03u;
  f[6] = UNIT;
  f[7] = MODBUS_FC_READ_COILS | MODBUS_EXCEPTION_FLAG;
  f[8] = 0x02u; /* ILLEGAL DATA ADDRESS */
  exc = 0u;
  CHECK_EQ(modbus_parse_read_coils(f, 9u, TID, UNIT, QTY, &bits, &exc),
           MODBUS_ERR_EXCEPTION);
  CHECK_EQ(exc, 0x02u);

  TESTS_REPORT("modbus_tcp");
}
```

- [ ] **Step 2: Add the suite to the Makefile**

```make
SUITES  := test_braille_dots test_plc_packet test_modbus_tcp
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
make -C tests
```
Expected: `modbus_tcp.c` does not exist.

- [ ] **Step 4: Create the header**

```c
/**
 * @file    modbus_tcp.h
 * @brief   MODBUS TCP Read Coils request builder and response parser.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * Frame encoding and decoding only: nothing here touches a socket, which is
 * what lets every malformed-response path be exercised off-target. The device
 * is always the client, and the only function code it uses is 0x01.
 *
 * A MODBUS TCP frame is a 7-byte MBAP header -- transaction id, protocol id,
 * length, unit id -- followed by the PDU.
 */

#ifndef MODBUS_TCP_H
#define MODBUS_TCP_H

#include <stdint.h>

/**
 * @defgroup modbus_tcp MODBUS TCP
 * @brief    Read Coils codec for the client side.
 * @{
 */

#define MODBUS_MBAP_LEN 7u        /**< MBAP header length.                */
#define MODBUS_PROTOCOL_ID 0u     /**< Always zero for MODBUS.            */
#define MODBUS_FC_READ_COILS 0x01u /**< Function code 1.                  */
#define MODBUS_EXCEPTION_FLAG 0x80u /**< Set in the FC of an exception.   */

/** @brief Most coils one call can carry, bounded by the uint16_t result. */
#define MODBUS_MAX_COILS 16u

/** @brief Request length: MBAP + FC + start address + quantity. */
#define MODBUS_READ_COILS_REQ_LEN 12u

/** @brief Bytes a coil count occupies on the wire. */
#define MODBUS_COIL_BYTES(count) (((count) + 7u) / 8u)

/** @brief Full length of a well-formed response for @p count coils. */
#define MODBUS_READ_COILS_RSP_LEN(count)                                       \
  (MODBUS_MBAP_LEN + 2u + MODBUS_COIL_BYTES(count))

/** @brief Outcome of parsing a response. */
typedef enum {
  MODBUS_OK = 0,              /**< Valid response; bits written.          */
  MODBUS_ERR_TOO_SHORT,       /**< Fewer bytes than any legal response.   */
  MODBUS_ERR_PROTOCOL_ID,     /**< MBAP protocol id was not zero.         */
  MODBUS_ERR_TRANSACTION_ID,  /**< Reply to some other request.           */
  MODBUS_ERR_UNIT_ID,         /**< Reply from some other unit.            */
  MODBUS_ERR_LENGTH_FIELD,    /**< MBAP length disagrees with the frame.  */
  MODBUS_ERR_FUNCTION,        /**< Not a Read Coils reply.                */
  MODBUS_ERR_BYTE_COUNT,      /**< Byte count wrong for the quantity.     */
  MODBUS_ERR_EXCEPTION,       /**< Slave returned an exception.           */
} modbus_status_t;

/**
 * @brief Build a Read Coils request.
 *
 * @param out            Destination buffer.
 * @param out_capacity   Bytes available in @p out.
 * @param transaction_id Id to echo back; increment it per request so a stale
 *                       reply can be told apart from a fresh one.
 * @param unit_id        MODBUS unit id.
 * @param start_address  First coil to read.
 * @param quantity       Coils to read, 1..@ref MODBUS_MAX_COILS.
 * @return Bytes written (@ref MODBUS_READ_COILS_REQ_LEN), or 0 when the
 *         buffer is too small or @p quantity is out of range.
 */
uint16_t modbus_build_read_coils(uint8_t *out, uint16_t out_capacity,
                                 uint16_t transaction_id, uint8_t unit_id,
                                 uint16_t start_address, uint16_t quantity);

/**
 * @brief Validate a Read Coils response and extract the coil states.
 *
 * Deliberately strict. A reply that decodes cleanly but answers an earlier
 * request would hand the reader stale values with nothing to show it went
 * wrong, so the transaction id is checked as carefully as the framing. Coil
 * bits above @p quantity are masked off so padding in the last byte cannot
 * appear as extra signals.
 *
 * @param frame              Received bytes.
 * @param length             Bytes in @p frame.
 * @param transaction_id     Id of the request being answered.
 * @param unit_id            Unit id of the request being answered.
 * @param quantity           Coils requested, 1..@ref MODBUS_MAX_COILS.
 * @param out_bits           Receives the coil states, LSB = first coil.
 *                           Untouched unless @ref MODBUS_OK is returned.
 * @param out_exception_code Receives the exception code when
 *                           @ref MODBUS_ERR_EXCEPTION is returned.
 * @return @ref MODBUS_OK, or the reason the frame was rejected.
 */
modbus_status_t modbus_parse_read_coils(const uint8_t *frame, uint16_t length,
                                        uint16_t transaction_id,
                                        uint8_t unit_id, uint16_t quantity,
                                        uint16_t *out_bits,
                                        uint8_t *out_exception_code);

/** @} */ // end of modbus_tcp

#endif // MODBUS_TCP_H
```

- [ ] **Step 5: Create the implementation**

```c
// ----------------------------------------------------------------------------
// modbus_tcp.c
//
// Read Coils framing for the client side. No sockets here on purpose: the
// caller owns the connection, so every rejection path in the parser can be
// exercised from a host test with a hand-built byte array.
// ----------------------------------------------------------------------------

#include "modbus_tcp.h"

// MBAP field offsets, shared by requests and responses.
#define MB_OFF_TRANSACTION_HI 0u
#define MB_OFF_TRANSACTION_LO 1u
#define MB_OFF_PROTOCOL_HI 2u
#define MB_OFF_PROTOCOL_LO 3u
#define MB_OFF_LENGTH_HI 4u
#define MB_OFF_LENGTH_LO 5u
#define MB_OFF_UNIT 6u
#define MB_OFF_FUNCTION 7u

// Request PDU offsets, after the function code.
#define MB_OFF_REQ_ADDRESS_HI 8u
#define MB_OFF_REQ_ADDRESS_LO 9u
#define MB_OFF_REQ_QUANTITY_HI 10u
#define MB_OFF_REQ_QUANTITY_LO 11u

// Response PDU offsets.
#define MB_OFF_RSP_BYTE_COUNT 8u
#define MB_OFF_RSP_DATA 9u
#define MB_OFF_RSP_EXCEPTION_CODE 8u

// The MBAP length field counts everything after it: unit id onwards.
#define MB_LENGTH_FIELD_OF(pdu_bytes) ((pdu_bytes) + 1u)
#define MB_REQ_PDU_BYTES 5u /* FC + address + quantity */
#define MB_EXCEPTION_RSP_LEN 9u
#define MB_BITS_PER_BYTE 8u

static uint16_t be16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << MB_BITS_PER_BYTE) | (uint16_t)p[1]);
}

uint16_t modbus_build_read_coils(uint8_t *out, uint16_t out_capacity,
                                 uint16_t transaction_id, uint8_t unit_id,
                                 uint16_t start_address, uint16_t quantity) {
  if (out == NULL || out_capacity < MODBUS_READ_COILS_REQ_LEN) {
    return 0u;
  }
  if (quantity == 0u || quantity > MODBUS_MAX_COILS) {
    return 0u;
  }

  out[MB_OFF_TRANSACTION_HI] = (uint8_t)(transaction_id >> MB_BITS_PER_BYTE);
  out[MB_OFF_TRANSACTION_LO] = (uint8_t)(transaction_id & 0xFFu);
  out[MB_OFF_PROTOCOL_HI] = (uint8_t)(MODBUS_PROTOCOL_ID >> MB_BITS_PER_BYTE);
  out[MB_OFF_PROTOCOL_LO] = (uint8_t)(MODBUS_PROTOCOL_ID & 0xFFu);
  out[MB_OFF_LENGTH_HI] = 0u;
  out[MB_OFF_LENGTH_LO] = (uint8_t)MB_LENGTH_FIELD_OF(MB_REQ_PDU_BYTES);
  out[MB_OFF_UNIT] = unit_id;
  out[MB_OFF_FUNCTION] = MODBUS_FC_READ_COILS;
  out[MB_OFF_REQ_ADDRESS_HI] = (uint8_t)(start_address >> MB_BITS_PER_BYTE);
  out[MB_OFF_REQ_ADDRESS_LO] = (uint8_t)(start_address & 0xFFu);
  out[MB_OFF_REQ_QUANTITY_HI] = (uint8_t)(quantity >> MB_BITS_PER_BYTE);
  out[MB_OFF_REQ_QUANTITY_LO] = (uint8_t)(quantity & 0xFFu);

  return MODBUS_READ_COILS_REQ_LEN;
}

modbus_status_t modbus_parse_read_coils(const uint8_t *frame, uint16_t length,
                                        uint16_t transaction_id,
                                        uint8_t unit_id, uint16_t quantity,
                                        uint16_t *out_bits,
                                        uint8_t *out_exception_code) {
  if (frame == NULL || out_bits == NULL || quantity == 0u ||
      quantity > MODBUS_MAX_COILS) {
    return MODBUS_ERR_TOO_SHORT;
  }
  // An exception reply is the shortest legal response, so anything below it
  // cannot be parsed at all.
  if (length < MB_EXCEPTION_RSP_LEN) {
    return MODBUS_ERR_TOO_SHORT;
  }
  if (be16(&frame[MB_OFF_PROTOCOL_HI]) != MODBUS_PROTOCOL_ID) {
    return MODBUS_ERR_PROTOCOL_ID;
  }
  if (be16(&frame[MB_OFF_TRANSACTION_HI]) != transaction_id) {
    return MODBUS_ERR_TRANSACTION_ID;
  }
  if (frame[MB_OFF_UNIT] != unit_id) {
    return MODBUS_ERR_UNIT_ID;
  }
  // The MBAP length field counts unit id onwards; if it disagrees with what
  // actually arrived, the framing is untrustworthy however well the rest
  // decodes.
  if (be16(&frame[MB_OFF_LENGTH_HI]) != (uint16_t)(length - MB_OFF_UNIT)) {
    return MODBUS_ERR_LENGTH_FIELD;
  }

  if (frame[MB_OFF_FUNCTION] ==
      (MODBUS_FC_READ_COILS | MODBUS_EXCEPTION_FLAG)) {
    if (out_exception_code != NULL) {
      *out_exception_code = frame[MB_OFF_RSP_EXCEPTION_CODE];
    }
    return MODBUS_ERR_EXCEPTION;
  }
  if (frame[MB_OFF_FUNCTION] != MODBUS_FC_READ_COILS) {
    return MODBUS_ERR_FUNCTION;
  }

  uint16_t expected_bytes = MODBUS_COIL_BYTES(quantity);
  if (frame[MB_OFF_RSP_BYTE_COUNT] != (uint8_t)expected_bytes) {
    return MODBUS_ERR_BYTE_COUNT;
  }
  if (length < MODBUS_READ_COILS_RSP_LEN(quantity)) {
    return MODBUS_ERR_TOO_SHORT;
  }

  uint16_t bits = 0u;
  for (uint16_t i = 0u; i < expected_bytes; ++i) {
    bits |= (uint16_t)((uint16_t)frame[MB_OFF_RSP_DATA + i]
                       << (i * MB_BITS_PER_BYTE));
  }
  // Padding in the final byte is not data; masking it off stops a slave that
  // sets those bits from inventing signals we never asked for.
  if (quantity < MODBUS_MAX_COILS) {
    bits &= (uint16_t)((1u << quantity) - 1u);
  }
  *out_bits = bits;
  return MODBUS_OK;
}
```

`NULL` requires `#include <stddef.h>` in `modbus_tcp.h`.

- [ ] **Step 6: Run the test to verify it passes**

```bash
make -C tests
```
Expected: three suites, all `0 failed`.

- [ ] **Step 7: Build and measure**

```bash
export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
cmake --build --preset Release && arm-none-eabi-size build/Release/single-cell-braille-controller.elf
```

- [ ] **Step 8: Commit**

```bash
git add App/Inc/modbus_tcp.h App/Src/modbus_tcp.c tests/test_modbus_tcp.c tests/Makefile
git commit -m "feat: add the MODBUS TCP Read Coils codec

Framing only -- the caller owns the socket. Keeping the codec free of I/O is
what makes the parser's rejection paths reachable from a host test with a
hand-built byte array, which is the only practical way to cover them.

The parser is deliberately strict about two things that are easy to get wrong
and silent when wrong. A reply carrying an earlier transaction id would hand
the reader stale coil states with nothing to indicate a problem, so the id is
checked as carefully as the framing and the MBAP length field is required to
agree with the bytes that actually arrived. And bits above the requested
quantity are masked off, so a slave that pads its last data byte cannot
inject signals we never asked for.

Release text: <record arm-none-eabi-size output here>.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: `reader_ui` — cursor, pending slot and presentation sequencer

Spec §6. Every row of the behaviour table. This module depends on no hardware: it reaches the world through a struct of function pointers, so the test drives it with a fake clock and asserts on a command log.

**One clarification to spec §6.** The table says a link fault makes `AWAITING_POLL` "return to its previous state". Restoring `LABEL` would resume a dwell timer that has since gone stale, so the implementation returns to `RESTING` when a package is held and `NO_DATA` when not. The cell keeps whatever glyph it last showed and the sequence does not lurch forward on its own. Update the spec table row when this task lands.

**Files:**
- Create: `App/Inc/reader_ui.h`, `App/Src/reader_ui.c`, `tests/test_reader_ui.c`
- Modify: `tests/Makefile`

**Interfaces:**
- Consumes: `plc_snapshot_t`, `plc_packet_field()`, `plc_snapshot_bit()`, `plc_snapshot_equal()`, `PLC_FIELD_COUNT` (Task 4); `BRAILLE_DOTS_ALL_FLAT`/`BRAILLE_DOTS_ALL_RAISED` (Task 1); the timings in `plc_config.h` (Task 3).
- Produces: `reader_event_t` (`READER_EV_NEXT`/`PREV`/`REPEAT`), `reader_sound_t` (`READER_SOUND_REQUEST_SENT`/`DATA_RECEIVED`/`PACKAGE_QUEUED`/`LINK_FAULT`/`BOUNDARY`), `reader_ui_io_t`, `void reader_ui_init(const reader_ui_io_t *io)`, `void reader_ui_on_event(reader_event_t)`, `void reader_ui_on_snapshot(plc_snapshot_t)`, `void reader_ui_on_link_fault(void)`, `void reader_ui_tick(void)`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_reader_ui.c`. The command log encoding is the whole trick: one character per side effect, so an expected sequence reads as a short string.

```c
/* Task 6 -- the reader state machine, every row of spec section 6.
 *
 * The module emits side effects through function pointers, so this suite
 * records them as one character each and compares whole sequences:
 *
 *   A B E S P T V R M L   a label glyph was rendered (the letter itself)
 *   -                     value glyph, all dots flat  (false)
 *   #                     value glyph, all dots raised (true)
 *   @                     a poll was requested
 *   ? ! * x .             REQUEST_SENT DATA_RECEIVED PACKAGE_QUEUED
 *                         LINK_FAULT BOUNDARY
 *
 * So "AE-" is "showed label A, then E, then a false value" -- one full item.
 */
#include "test_support.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "motion_planner.h"

void move_to_angle(disk_angles_t a) { (void)a; }
void initialize_motors(UART_HandleTypeDef *a, UART_HandleTypeDef *b,
                       TIM_HandleTypeDef *c, TIM_HandleTypeDef *d) {
  (void)a; (void)b; (void)c; (void)d;
}
bool calibrate_zero_position(void) { return true; }

#include "plc_packet.c"
#include "reader_ui.c"

/* ---- recording IO ---- */
static char log_buf[512];
static size_t log_len;
static uint32_t fake_now;

static void log_put(char c) {
  if (log_len + 1u < sizeof log_buf) {
    log_buf[log_len++] = c;
  }
  log_buf[log_len] = '\0';
}
static void log_reset(void) {
  log_len = 0u;
  log_buf[0] = '\0';
}
static void t_render_char(uint8_t c) { log_put((char)c); }
static void t_render_dots(uint8_t dots) {
  log_put(dots == BRAILLE_DOTS_ALL_RAISED
              ? '#'
              : (dots == BRAILLE_DOTS_ALL_FLAT ? '-' : '~'));
}
static void t_play_sound(reader_sound_t s) {
  switch (s) {
  case READER_SOUND_REQUEST_SENT:   log_put('?'); break;
  case READER_SOUND_DATA_RECEIVED:  log_put('!'); break;
  case READER_SOUND_PACKAGE_QUEUED: log_put('*'); break;
  case READER_SOUND_LINK_FAULT:     log_put('x'); break;
  case READER_SOUND_BOUNDARY:       log_put('.'); break;
  default:                          log_put('~'); break;
  }
}
static void t_request_poll(void) { log_put('@'); }
static uint32_t t_now(void) { return fake_now; }

static const reader_ui_io_t test_io = {
    .render_char = t_render_char,
    .render_dots = t_render_dots,
    .play_sound = t_play_sound,
    .request_poll = t_request_poll,
    .now_ms = t_now,
};

/* ---- helpers ---- */
static plc_snapshot_t snap(uint16_t bits) {
  plc_snapshot_t s = {bits};
  return s;
}
/* Move the clock and let the sequencer act on it. */
static void advance(uint32_t ms) {
  fake_now += ms;
  reader_ui_tick();
}
/* Move the clock without giving the sequencer a chance to run, for testing
 * rate limits without also advancing a dwell. */
static void jump(uint32_t ms) { fake_now += ms; }

/* Let the current item play to its value glyph. Two dwells is enough for the
 * longest label; a one-glyph label simply finishes on the first. */
static void finish_item(void) {
  advance(PLC_LABEL_DWELL_MS);
  advance(PLC_LABEL_DWELL_MS);
}
/* Press NEXT and play the item out, for fields 0..index. */
static void walk_to(uint8_t index) {
  for (uint8_t i = 0u; i <= index; ++i) {
    reader_ui_on_event(READER_EV_NEXT);
    finish_item();
  }
}
static void start(uint16_t bits) {
  fake_now = 1000u;
  reader_ui_init(&test_io);
  reader_ui_on_snapshot(snap(bits));
  log_reset();
}

#define LAST_INDEX ((uint8_t)(PLC_FIELD_COUNT - 1u))

int main(void) {
  /* -- NEXT with no data ever received asks the PLC and waits -- */
  fake_now = 1000u;
  reader_ui_init(&test_io);
  log_reset();
  reader_ui_on_event(READER_EV_NEXT);
  CHECK_STR_EQ(log_buf, "@?");
  reader_ui_on_event(READER_EV_NEXT); /* ignored while a request is in flight */
  reader_ui_on_event(READER_EV_PREV);
  reader_ui_on_event(READER_EV_REPEAT);
  CHECK_STR_EQ(log_buf, "@?");
  reader_ui_on_snapshot(snap(0x0000u)); /* the answer */
  CHECK_STR_EQ(log_buf, "@?!A");
  advance(PLC_LABEL_DWELL_MS);
  CHECK_STR_EQ(log_buf, "@?!AE");
  advance(PLC_LABEL_DWELL_MS);
  CHECK_STR_EQ(log_buf, "@?!AE-");
  advance(PLC_LABEL_DWELL_MS * 4u); /* resting: further ticks do nothing */
  CHECK_STR_EQ(log_buf, "@?!AE-");

  /* -- a dwell that has not elapsed does not advance the glyph -- */
  start(0x0000u);
  reader_ui_on_event(READER_EV_NEXT);
  CHECK_STR_EQ(log_buf, "A");
  advance(PLC_LABEL_DWELL_MS - 1u);
  CHECK_STR_EQ(log_buf, "A");
  advance(1u);
  CHECK_STR_EQ(log_buf, "AE");

  /* -- the first background snapshot is adopted but nothing is rendered -- */
  fake_now = 1000u;
  reader_ui_init(&test_io);
  log_reset();
  reader_ui_on_snapshot(snap(0x07FFu));
  CHECK_STR_EQ(log_buf, "!");
  reader_ui_on_event(READER_EV_NEXT);
  finish_item();
  CHECK_STR_EQ(log_buf, "!AE#"); /* every coil true */

  /* -- labels of both lengths, and true/false glyphs -- */
  start((uint16_t)(1u << 4)); /* only SENSOR PRESENCA true */
  walk_to(4u);
  CHECK_STR_EQ(log_buf, "AE-AS-BE-BS-P#");

  /* -- the whole package, to pin the reading order -- */
  start(0x0000u);
  walk_to(LAST_INDEX);
  CHECK_STR_EQ(log_buf, "AE-AS-BE-BS-P-S-T-V-R-M-L-");

  /* -- PREV steps back and re-presents from the first glyph -- */
  start(0x0000u);
  walk_to(1u);
  log_reset();
  reader_ui_on_event(READER_EV_PREV);
  CHECK_STR_EQ(log_buf, "A");
  finish_item();
  CHECK_STR_EQ(log_buf, "AE-");

  /* -- PREV at the first field blips and leaves the discs alone -- */
  log_reset();
  reader_ui_on_event(READER_EV_PREV);
  CHECK_STR_EQ(log_buf, ".");
  reader_ui_on_event(READER_EV_PREV);
  CHECK_STR_EQ(log_buf, "..");

  /* -- REPEAT replays the current item from its first glyph -- */
  start(0x0000u);
  walk_to(0u);
  log_reset();
  reader_ui_on_event(READER_EV_REPEAT);
  finish_item();
  CHECK_STR_EQ(log_buf, "AE-");

  /* -- REPEAT before reading has started has nothing to repeat -- */
  start(0x0000u);
  reader_ui_on_event(READER_EV_REPEAT);
  CHECK_STR_EQ(log_buf, ".");

  /* -- a differing background snapshot queues and chirps -- */
  start(0x0000u);
  walk_to(0u);
  log_reset();
  reader_ui_on_snapshot(snap(0x0001u));
  CHECK_STR_EQ(log_buf, "*");
  /* an identical one is silent */
  log_reset();
  reader_ui_on_snapshot(snap(0x0000u));
  CHECK_STR_EQ(log_buf, "");

  /* -- the chirp is rate limited -- */
  start(0x0000u);
  walk_to(0u);
  log_reset();
  reader_ui_on_snapshot(snap(0x0001u));
  CHECK_STR_EQ(log_buf, "*");
  reader_ui_on_snapshot(snap(0x0002u)); /* too soon: queued, no chirp */
  CHECK_STR_EQ(log_buf, "*");
  jump(PLC_QUEUED_SOUND_MIN_INTERVAL_MS);
  reader_ui_on_snapshot(snap(0x0004u));
  CHECK_STR_EQ(log_buf, "**");

  /* -- NEXT past the last field adopts a pending package and clears it -- */
  start(0x0000u);
  walk_to(LAST_INDEX);
  reader_ui_on_snapshot(snap(0x0001u)); /* SMEMA A IN went true */
  log_reset();
  reader_ui_on_event(READER_EV_NEXT);
  CHECK_STR_EQ(log_buf, "!A");
  finish_item();
  CHECK_STR_EQ(log_buf, "!AE#"); /* the adopted package, not the old one */
  /* the slot is empty now, so going round again must ask the PLC */
  walk_to(LAST_INDEX - 1u);
  log_reset();
  reader_ui_on_event(READER_EV_NEXT);
  CHECK_STR_EQ(log_buf, "@?");

  /* -- NEXT past the last field with nothing pending asks the PLC -- */
  start(0x0000u);
  walk_to(LAST_INDEX);
  log_reset();
  reader_ui_on_event(READER_EV_NEXT);
  CHECK_STR_EQ(log_buf, "@?");
  reader_ui_on_snapshot(snap(0x0000u)); /* unchanged, still adopted */
  CHECK_STR_EQ(log_buf, "@?!A");

  /* -- a request that never gets answered faults, and the package survives -- */
  start(0x0000u);
  walk_to(LAST_INDEX);
  log_reset();
  reader_ui_on_event(READER_EV_NEXT);
  CHECK_STR_EQ(log_buf, "@?");
  advance(PLC_RESPONSE_TIMEOUT_MS);
  CHECK_STR_EQ(log_buf, "@?x");
  /* back to resting on the package it already had: REPEAT still works */
  log_reset();
  reader_ui_on_event(READER_EV_REPEAT);
  CHECK_STR_EQ(log_buf, "L");

  /* -- the fault sound is rate limited -- */
  start(0x0000u);
  log_reset();
  reader_ui_on_link_fault();
  CHECK_STR_EQ(log_buf, "x");
  reader_ui_on_link_fault();
  CHECK_STR_EQ(log_buf, "x");
  jump(PLC_FAULT_SOUND_MIN_INTERVAL_MS);
  reader_ui_on_link_fault();
  CHECK_STR_EQ(log_buf, "xx");

  /* -- a fault with no package at all leaves the reader asking again -- */
  fake_now = 1000u;
  reader_ui_init(&test_io);
  log_reset();
  reader_ui_on_event(READER_EV_NEXT);
  advance(PLC_RESPONSE_TIMEOUT_MS);
  CHECK_STR_EQ(log_buf, "@?x");
  log_reset();
  reader_ui_on_event(READER_EV_NEXT);
  CHECK_STR_EQ(log_buf, "@?");

  /* -- the tick counter wrapping must not freeze a dwell -- */
  reader_ui_init(&test_io);
  fake_now = 0xFFFFFF00u;
  reader_ui_on_snapshot(snap(0x0000u));
  log_reset();
  reader_ui_on_event(READER_EV_NEXT);
  CHECK_STR_EQ(log_buf, "A");
  advance(PLC_LABEL_DWELL_MS); /* crosses 0xFFFFFFFF */
  CHECK_STR_EQ(log_buf, "AE");

  TESTS_REPORT("reader_ui");
}
```

- [ ] **Step 2: Add the suite to the Makefile**

```make
SUITES  := test_braille_dots test_plc_packet test_modbus_tcp test_reader_ui
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
make -C tests
```
Expected: `reader_ui.c` does not exist.

- [ ] **Step 4: Create the header**

```c
/**
 * @file    reader_ui.h
 * @brief   Navigation and presentation of the PLC package on the cell.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * The cell shows one glyph at a time, so a key/value pair is a sequence: the
 * label's letter or letters, each held for a dwell, then the value as all
 * dots flat (false) or all raised (true). The cell rests on the value, so a
 * reader always ends with a finger on the answer.
 *
 * This module owns the reading cursor, the single pending-package slot, and
 * the dwell timing. It reaches hardware only through @ref reader_ui_io_t,
 * which is what lets the whole behaviour be driven off-target from a test.
 */

#ifndef READER_UI_H
#define READER_UI_H

#include <stdint.h>

#include "plc_packet.h"

/**
 * @defgroup reader_ui Reader UI
 * @brief    Cursor, pending package and presentation sequencing.
 * @{
 */

/** @brief A button press, already debounced. */
typedef enum {
  READER_EV_NONE = 0, /**< Nothing happened.                            */
  READER_EV_NEXT,     /**< Advance one field, or fetch a new package.   */
  READER_EV_PREV,     /**< Go back one field.                           */
  READER_EV_REPEAT,   /**< Replay the current field from its first glyph. */
} reader_event_t;

/** @brief Something the reader needs to hear about. */
typedef enum {
  READER_SOUND_NONE = 0,        /**< Nothing to play.                    */
  READER_SOUND_REQUEST_SENT,    /**< A request went out to the PLC.      */
  READER_SOUND_DATA_RECEIVED,   /**< A package arrived and is now current. */
  READER_SOUND_PACKAGE_QUEUED,  /**< Newer data is waiting behind this one. */
  READER_SOUND_LINK_FAULT,      /**< The link is unhappy.                */
  READER_SOUND_BOUNDARY,        /**< Nothing there: at field 0, or nothing
                                     to repeat.                          */
} reader_sound_t;

/**
 * @brief Everything the reader needs from the outside world.
 *
 * Supplied once at init. @c render_char and @c render_dots may block -- on
 * hardware they turn the discs -- and the dwell is measured from after they
 * return.
 */
typedef struct {
  void (*render_char)(uint8_t latin1_char); /**< Show a label glyph.     */
  void (*render_dots)(uint8_t dots);        /**< Show a raw dot pattern. */
  void (*play_sound)(reader_sound_t sound);  /**< Play a notification.    */
  void (*request_poll)(void); /**< Ask the link for an out-of-cycle poll. */
  uint32_t (*now_ms)(void);   /**< Monotonic milliseconds; may wrap.      */
} reader_ui_io_t;

/**
 * @brief Reset the reader and bind it to its IO.
 *
 * Clears the cursor, the current and pending packages, and the sound rate
 * limiters. The cell is left as it is; nothing is rendered.
 *
 * @param io Callbacks to use. Must outlive the reader; not copied.
 */
void reader_ui_init(const reader_ui_io_t *io);

/**
 * @brief Deliver a debounced button press.
 *
 * Presses arriving while a request is in flight are dropped, so an impatient
 * reader cannot queue up navigation that would run after the answer lands.
 *
 * @param event The press.
 */
void reader_ui_on_event(reader_event_t event);

/**
 * @brief Deliver a freshly polled package.
 *
 * Whether this answers an outstanding request or is just the background
 * poller is decided from the reader's own state, not from the snapshot: a
 * package arriving while a request is in flight is that request's answer.
 * Otherwise a package identical to the current one is ignored, and a
 * differing one goes to the pending slot, overwriting whatever was there.
 *
 * @param snapshot The coil states just read.
 */
void reader_ui_on_snapshot(plc_snapshot_t snapshot);

/**
 * @brief Report that the link could not deliver.
 *
 * The current package is never discarded: a reader mid-package keeps reading
 * what they have and only hears that the link is unhappy.
 */
void reader_ui_on_link_fault(void);

/**
 * @brief Service the dwell timer and the request deadline.
 *
 * Call once per main-loop pass. Does nothing unless a dwell has elapsed or a
 * request has timed out.
 */
void reader_ui_tick(void);

/** @} */ // end of reader_ui

#endif // READER_UI_H
```

- [ ] **Step 5: Create the implementation**

```c
// ----------------------------------------------------------------------------
// reader_ui.c
//
// The reading model. One press of NEXT plays a whole item -- label glyphs
// then the value -- and leaves the cell on the value.
//
// Two things here are deliberate and easy to undo by accident:
//
//  * There is one pending slot, not a queue. Reading eleven fields takes tens
//    of seconds, so a queue would put the reader further and further behind
//    the line. Overwriting means stepping past the last field always lands on
//    the current state of the machine.
//
//  * A link fault never discards the current package. Losing the link is not
//    a reason to take away what the reader is holding.
// ----------------------------------------------------------------------------

#include "reader_ui.h"

#include "braille_disc.h"
#include "plc_config.h"
#include "plc_packet.h"

/** @brief Cursor value before the first field has been shown. */
#define READER_CURSOR_NOT_STARTED (-1)

/** @brief Highest field index; the cursor is a signed index into the table. */
#define READER_LAST_FIELD ((int8_t)(PLC_FIELD_COUNT - 1u))

typedef enum {
  READER_STATE_NO_DATA = 0,   /**< No package held; cell blank.           */
  READER_STATE_AWAITING_POLL, /**< An explicit request is in flight.      */
  READER_STATE_LABEL,         /**< Showing a label glyph; dwell armed.    */
  READER_STATE_RESTING,       /**< Holding a package; idle on a value.    */
} reader_state_t;

// A sound that may only play so often. Without the "seen" flag the first
// sound after boot would be compared against a zero timestamp, which at
// tick 0 would suppress it.
typedef struct {
  uint32_t last_ms;
  bool seen;
} rate_limit_t;

static struct {
  const reader_ui_io_t *io;
  reader_state_t state;
  int8_t cursor;
  uint8_t glyph_index;
  uint32_t dwell_started_ms;
  uint32_t request_started_ms;
  bool has_current;
  plc_snapshot_t current;
  bool pending_valid;
  plc_snapshot_t pending;
  rate_limit_t queued_sound;
  rate_limit_t fault_sound;
} reader;

// Unsigned subtraction, so a wrapping millisecond counter still measures a
// correct elapsed time.
static bool elapsed(uint32_t since_ms, uint32_t interval_ms) {
  return (uint32_t)(reader.io->now_ms() - since_ms) >= interval_ms;
}

static bool rate_limit_allows(rate_limit_t *limit, uint32_t interval_ms) {
  if (limit->seen && !elapsed(limit->last_ms, interval_ms)) {
    return false;
  }
  limit->last_ms = reader.io->now_ms();
  limit->seen = true;
  return true;
}

static void play(reader_sound_t sound) { reader.io->play_sound(sound); }

// Show the first glyph of the field the cursor is on and arm the dwell. The
// clock is read after the render returns, because on hardware the render
// blocks while the discs turn and the dwell is meant to start once the glyph
// is actually there.
static void present_current_field(void) {
  const plc_field_t *field = plc_packet_field((uint8_t)reader.cursor);
  if (field == NULL) {
    return;
  }
  reader.glyph_index = 0u;
  reader.io->render_char(field->label[0]);
  reader.dwell_started_ms = reader.io->now_ms();
  reader.state = READER_STATE_LABEL;
}

static void render_value(void) {
  bool value = plc_snapshot_bit(reader.current, (uint8_t)reader.cursor);
  reader.io->render_dots(value ? BRAILLE_DOTS_ALL_RAISED
                               : BRAILLE_DOTS_ALL_FLAT);
  reader.state = READER_STATE_RESTING;
}

static void start_request(void) {
  reader.io->request_poll();
  play(READER_SOUND_REQUEST_SENT);
  reader.request_started_ms = reader.io->now_ms();
  reader.state = READER_STATE_AWAITING_POLL;
}

// Make a package the one being read, from its first field.
static void adopt(plc_snapshot_t snapshot) {
  reader.current = snapshot;
  reader.has_current = true;
  reader.pending_valid = false;
  reader.cursor = 0;
  play(READER_SOUND_DATA_RECEIVED);
  present_current_field();
}

void reader_ui_init(const reader_ui_io_t *io) {
  static const plc_snapshot_t empty = {0u};
  reader.io = io;
  reader.state = READER_STATE_NO_DATA;
  reader.cursor = READER_CURSOR_NOT_STARTED;
  reader.glyph_index = 0u;
  reader.dwell_started_ms = 0u;
  reader.request_started_ms = 0u;
  reader.has_current = false;
  reader.current = empty;
  reader.pending_valid = false;
  reader.pending = empty;
  reader.queued_sound.seen = false;
  reader.queued_sound.last_ms = 0u;
  reader.fault_sound.seen = false;
  reader.fault_sound.last_ms = 0u;
}

void reader_ui_on_event(reader_event_t event) {
  // A press arriving while a request is in flight is dropped rather than
  // queued: acting on it after the answer lands would move the reader
  // somewhere they asked to go a second ago and have since been answered.
  if (reader.state == READER_STATE_AWAITING_POLL) {
    return;
  }

  switch (event) {
  case READER_EV_NEXT:
    if (!reader.has_current) {
      start_request();
    } else if (reader.cursor < READER_LAST_FIELD) {
      ++reader.cursor;
      present_current_field();
    } else if (reader.pending_valid) {
      adopt(reader.pending);
    } else {
      start_request();
    }
    break;

  case READER_EV_PREV:
    if (reader.has_current && reader.cursor > 0) {
      --reader.cursor;
      present_current_field();
    } else {
      play(READER_SOUND_BOUNDARY);
    }
    break;

  case READER_EV_REPEAT:
    if (reader.has_current && reader.cursor >= 0) {
      present_current_field();
    } else {
      play(READER_SOUND_BOUNDARY);
    }
    break;

  case READER_EV_NONE:
  default:
    break;
  }
}

void reader_ui_on_snapshot(plc_snapshot_t snapshot) {
  if (reader.state == READER_STATE_AWAITING_POLL) {
    adopt(snapshot);
    return;
  }

  // The first package ever seen is adopted but not rendered: the reader is
  // told it arrived and decides when to start reading.
  if (!reader.has_current) {
    reader.current = snapshot;
    reader.has_current = true;
    reader.cursor = READER_CURSOR_NOT_STARTED;
    reader.state = READER_STATE_RESTING;
    play(READER_SOUND_DATA_RECEIVED);
    return;
  }

  if (plc_snapshot_equal(snapshot, reader.current)) {
    return;
  }

  reader.pending = snapshot;
  reader.pending_valid = true;
  if (rate_limit_allows(&reader.queued_sound,
                        PLC_QUEUED_SOUND_MIN_INTERVAL_MS)) {
    play(READER_SOUND_PACKAGE_QUEUED);
  }
}

void reader_ui_on_link_fault(void) {
  if (rate_limit_allows(&reader.fault_sound, PLC_FAULT_SOUND_MIN_INTERVAL_MS)) {
    play(READER_SOUND_LINK_FAULT);
  }
  if (reader.state == READER_STATE_AWAITING_POLL) {
    // Resting rather than whatever was showing before the request: the dwell
    // that was running then is stale now, and resuming it would make the
    // sequence lurch forward on its own.
    reader.state =
        reader.has_current ? READER_STATE_RESTING : READER_STATE_NO_DATA;
  }
}

void reader_ui_tick(void) {
  switch (reader.state) {
  case READER_STATE_LABEL: {
    if (!elapsed(reader.dwell_started_ms, PLC_LABEL_DWELL_MS)) {
      return;
    }
    const plc_field_t *field = plc_packet_field((uint8_t)reader.cursor);
    if (field == NULL) {
      reader.state = READER_STATE_RESTING;
      return;
    }
    if ((uint8_t)(reader.glyph_index + 1u) < field->glyph_count) {
      ++reader.glyph_index;
      reader.io->render_char(field->label[reader.glyph_index]);
      reader.dwell_started_ms = reader.io->now_ms();
      return;
    }
    render_value();
    return;
  }

  case READER_STATE_AWAITING_POLL:
    if (elapsed(reader.request_started_ms, PLC_RESPONSE_TIMEOUT_MS)) {
      reader_ui_on_link_fault();
    }
    return;

  case READER_STATE_NO_DATA:
  case READER_STATE_RESTING:
  default:
    return;
  }
}
```

`reader_ui.c` uses `bool`, so `reader_ui.h` needs `#include <stdbool.h>` (it arrives via `plc_packet.h`, but include it explicitly).

- [ ] **Step 6: Run the test to verify it passes**

```bash
make -C tests
```
Expected: four suites, all `0 failed`. If the full-package check fails, print `log_buf` — the failure message already shows both strings, and the differing character identifies which field or sound went wrong.

- [ ] **Step 7: Build and measure**

```bash
export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
cmake --build --preset Release && arm-none-eabi-size build/Release/single-cell-braille-controller.elf
```

- [ ] **Step 8: Update the spec's link-fault row**

In `docs/superpowers/specs/2026-09-09-modbus-tcp-braille-reader-design.md` §6, change the `link fault` row's behaviour from
`` `AWAITING_POLL` returns to its previous state ``
to
`` `AWAITING_POLL` returns to `RESTING`, or `NO_DATA` when no package is held ``
and note in §10 that the reason is a stale dwell timer.

- [ ] **Step 9: Commit**

```bash
git add App/Inc/reader_ui.h App/Src/reader_ui.c tests/test_reader_ui.c tests/Makefile docs/superpowers/specs/
git commit -m "feat: add the reader UI state machine

One press of NEXT plays a whole item -- label glyphs, then the value as all
dots flat or all raised -- and the cell rests on the value so the reader ends
with a finger on the answer. Dwell is measured from after the render returns,
because on hardware the render blocks while the discs turn.

The module reaches hardware only through a struct of function pointers, which
is the point of the decomposition: verifying eleven-field navigation on real
discs costs about 45 seconds per attempt, so the test drives the machine with
a fake clock and compares whole sequences of side effects encoded one
character each. \"AE-\" is a full item.

Three behaviours are deliberate and would be easy to undo by accident. There
is one pending slot rather than a queue, because a queue would leave the
reader progressively further behind a running line; overwriting means stepping
past the last field always lands on the current state. A link fault never
discards the current package. And presses arriving while a request is in
flight are dropped, not queued, so navigation cannot fire after the answer it
would have raced.

Deviates from spec section 6 in one place, and the spec is updated to match: a
fault leaves AWAITING_POLL for RESTING rather than for whatever was showing
before, because that state's dwell timer is stale and resuming it would make
the sequence advance on its own.

Release text: <record arm-none-eabi-size output here>.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Phase 2 — Hardware ports

### Task 7: `buzzer` — the tone-pattern player

Spec §7 and decision §3.7. TIM4_CH1 on PB6 is already configured by `MX_TIM4_Init()` with prescaler 47 on the 48 MHz APB1 timer clock, giving a 1 MHz counter tick, so `ARR = 1000000/f - 1` sets the pitch and `CCR = (ARR+1)/2` gives 50 % duty.

**A correction to make while implementing.** Spec §3.7 and §7 claim patterns are bounded at ≤ 400 ms, but §7's `LINK_FAULT` row (200 ms, gap, 200 ms) totals 480 ms. The 200 ms tones are worth keeping — a fault should sound unmistakably slower than everything else — so raise the stated bound to 500 ms in both places rather than shortening the tone. Actual totals: `REQUEST_SENT` 160 ms, `DATA_RECEIVED` 150 ms, `PACKAGE_QUEUED` 220 ms, `LINK_FAULT` 480 ms, `BOUNDARY` 40 ms.

**Files:**
- Create: `App/Inc/buzzer.h`, `App/Src/buzzer.c`
- Modify: `docs/superpowers/specs/2026-09-09-modbus-tcp-braille-reader-design.md`

**Interfaces:**
- Consumes: `reader_sound_t` (Task 6).
- Produces: `void buzzer_init(TIM_HandleTypeDef *htim, uint32_t channel)`, `void buzzer_play(reader_sound_t sound)`.

`buzzer.h` deliberately takes `reader_sound_t` rather than defining a parallel enum. The five sounds are this device's whole audible vocabulary and the buzzer exists to play exactly them; a second enum plus a mapping table would be indirection with no second caller to justify it.

- [ ] **Step 1: Create the header**

```c
/**
 * @file    buzzer.h
 * @brief   Notification tones on the piezo buzzer.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * The reader cannot see the cell, so every event that changes what the discs
 * mean has to be audible. Each sound is a short pattern of PWM tones,
 * separated from the others by pitch and by texture -- a rising pair, a
 * single tone, a triple chirp -- so they stay apart through a small piezo in
 * a noisy room.
 */

#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

#include "reader_ui.h"
#include "stm32f1xx_hal.h"

/**
 * @defgroup buzzer Buzzer
 * @brief    Audible notifications for the reader.
 * @{
 */

/**
 * @brief Bind the buzzer to its timer channel.
 *
 * The timer must already be configured for PWM with a 1 MHz counter tick,
 * which is what @c MX_TIM4_Init() sets up (prescaler 47 on the 48 MHz APB1
 * timer clock). Only the period and compare value are changed afterwards.
 *
 * @param htim    Timer driving the buzzer pin.
 * @param channel Timer channel, e.g. @c TIM_CHANNEL_1.
 */
void buzzer_init(TIM_HandleTypeDef *htim, uint32_t channel);

/**
 * @brief Play one notification and return when it has finished.
 *
 * Blocking, and deliberately so. PWM keeps sounding in hardware with no
 * software involvement, so a tone started immediately before a blocking disc
 * move would sound for the whole move unless something stopped it. Every
 * pattern is short -- 480 ms at the very worst, most under 250 ms -- against
 * disc moves that already block for around a second, and button presses are
 * latched by their interrupt throughout, so nothing is lost.
 *
 * @param sound Which notification to play. @c READER_SOUND_NONE and any
 *              unknown value do nothing.
 */
void buzzer_play(reader_sound_t sound);

/** @} */ // end of buzzer

#endif // BUZZER_H
```

- [ ] **Step 2: Create the implementation**

```c
// ----------------------------------------------------------------------------
// buzzer.c
//
// Tone patterns for the five reader notifications. The timer arrives already
// configured for a 1 MHz counter tick, so pitch is just a period: at 1 MHz,
// ARR = 1000000/f - 1, and a compare of half the period is 50% duty.
//
// The patterns are separated by texture as well as pitch. Two sounds that
// differ only in frequency are hard to tell apart on a small piezo, so
// "asking" is a rising pair, "received" is one longer tone, and "something is
// waiting" is a fast triple chirp.
// ----------------------------------------------------------------------------

#include "buzzer.h"

#include <stddef.h>

/** @brief Counter tick the timer is configured for, in hertz. */
#define BUZZER_TIMER_TICK_HZ 1000000u

/** @brief Duty cycle divisor: half the period is 50%. */
#define BUZZER_DUTY_DIVISOR 2u

/**
 * @name Tones
 * @{
 */
#define BUZZER_TONE_SILENT_HZ 0u    /**< Output off for this step.       */
#define BUZZER_TONE_FAULT_HZ 220u   /**< Low: something is wrong.        */
#define BUZZER_TONE_EDGE_HZ 330u    /**< Low blip: nothing there.        */
#define BUZZER_TONE_ASK_LOW_HZ 880u /**< First half of the rising pair.  */
#define BUZZER_TONE_ASK_HIGH_HZ 1175u /**< Second half.                  */
#define BUZZER_TONE_CONFIRM_HZ 1568u  /**< Data landed.                  */
#define BUZZER_TONE_ALERT_HZ 2093u    /**< Chirp: data is waiting.       */
/** @} */

/**
 * @name Step durations
 * @{
 */
#define BUZZER_CHIRP_MS 40u
#define BUZZER_BEEP_MS 60u
#define BUZZER_CONFIRM_MS 150u
#define BUZZER_FAULT_MS 200u
#define BUZZER_GAP_BEEP_MS 40u
#define BUZZER_GAP_CHIRP_MS 50u
#define BUZZER_GAP_FAULT_MS 80u
/** @} */

/** @brief One tone, or silence when @c frequency_hz is zero. */
typedef struct {
  uint16_t frequency_hz;
  uint16_t duration_ms;
} buzzer_step_t;

/** @brief A named sequence of tones. */
typedef struct {
  const buzzer_step_t *steps;
  uint8_t step_count;
} buzzer_pattern_t;

// Rising pair: the device is asking the PLC something. 160 ms.
static const buzzer_step_t buzzer_steps_request[] = {
    {BUZZER_TONE_ASK_LOW_HZ, BUZZER_BEEP_MS},
    {BUZZER_TONE_SILENT_HZ, BUZZER_GAP_BEEP_MS},
    {BUZZER_TONE_ASK_HIGH_HZ, BUZZER_BEEP_MS},
};

// One longer, higher tone: the answer arrived and is now what you are
// reading. 150 ms.
static const buzzer_step_t buzzer_steps_received[] = {
    {BUZZER_TONE_CONFIRM_HZ, BUZZER_CONFIRM_MS},
};

// Fast triple chirp, clearly not the single confirm tone: newer data is
// waiting behind the package in your hands. 220 ms.
static const buzzer_step_t buzzer_steps_queued[] = {
    {BUZZER_TONE_ALERT_HZ, BUZZER_CHIRP_MS},
    {BUZZER_TONE_SILENT_HZ, BUZZER_GAP_CHIRP_MS},
    {BUZZER_TONE_ALERT_HZ, BUZZER_CHIRP_MS},
    {BUZZER_TONE_SILENT_HZ, BUZZER_GAP_CHIRP_MS},
    {BUZZER_TONE_ALERT_HZ, BUZZER_CHIRP_MS},
};

// Slow low double: the link is unhappy. Deliberately the longest pattern, so
// it is unmistakable. 480 ms.
static const buzzer_step_t buzzer_steps_fault[] = {
    {BUZZER_TONE_FAULT_HZ, BUZZER_FAULT_MS},
    {BUZZER_TONE_SILENT_HZ, BUZZER_GAP_FAULT_MS},
    {BUZZER_TONE_FAULT_HZ, BUZZER_FAULT_MS},
};

// A single short low blip: you are already at the first field, or there is
// nothing to repeat. 40 ms.
static const buzzer_step_t buzzer_steps_edge[] = {
    {BUZZER_TONE_EDGE_HZ, BUZZER_CHIRP_MS},
};

#define BUZZER_PATTERN(name)                                                   \
  { buzzer_steps_##name,                                                       \
    (uint8_t)(sizeof buzzer_steps_##name / sizeof buzzer_steps_##name[0]) }

static const buzzer_pattern_t buzzer_pattern_request = BUZZER_PATTERN(request);
static const buzzer_pattern_t buzzer_pattern_received =
    BUZZER_PATTERN(received);
static const buzzer_pattern_t buzzer_pattern_queued = BUZZER_PATTERN(queued);
static const buzzer_pattern_t buzzer_pattern_fault = BUZZER_PATTERN(fault);
static const buzzer_pattern_t buzzer_pattern_edge = BUZZER_PATTERN(edge);

static TIM_HandleTypeDef *buzzer_timer;
static uint32_t buzzer_channel;

static const buzzer_pattern_t *buzzer_pattern_for(reader_sound_t sound) {
  switch (sound) {
  case READER_SOUND_REQUEST_SENT:
    return &buzzer_pattern_request;
  case READER_SOUND_DATA_RECEIVED:
    return &buzzer_pattern_received;
  case READER_SOUND_PACKAGE_QUEUED:
    return &buzzer_pattern_queued;
  case READER_SOUND_LINK_FAULT:
    return &buzzer_pattern_fault;
  case READER_SOUND_BOUNDARY:
    return &buzzer_pattern_edge;
  case READER_SOUND_NONE:
  default:
    return NULL;
  }
}

// Retune and restart the PWM. The timer was configured with auto-reload
// preload disabled, so a new period takes effect at once rather than at the
// next update event; the counter is reset so a step always begins at the
// start of a cycle and cannot emit a truncated first pulse.
static void buzzer_tone(uint16_t frequency_hz) {
  if (frequency_hz == BUZZER_TONE_SILENT_HZ) {
    (void)HAL_TIM_PWM_Stop(buzzer_timer, buzzer_channel);
    return;
  }
  uint32_t period = (BUZZER_TIMER_TICK_HZ / (uint32_t)frequency_hz) - 1u;
  __HAL_TIM_SET_AUTORELOAD(buzzer_timer, period);
  __HAL_TIM_SET_COMPARE(buzzer_timer, buzzer_channel,
                        (period + 1u) / BUZZER_DUTY_DIVISOR);
  __HAL_TIM_SET_COUNTER(buzzer_timer, 0u);
  (void)HAL_TIM_PWM_Start(buzzer_timer, buzzer_channel);
}

void buzzer_init(TIM_HandleTypeDef *htim, uint32_t channel) {
  buzzer_timer = htim;
  buzzer_channel = channel;
}

void buzzer_play(reader_sound_t sound) {
  const buzzer_pattern_t *pattern = buzzer_pattern_for(sound);
  if (buzzer_timer == NULL || pattern == NULL) {
    return;
  }
  for (uint8_t i = 0u; i < pattern->step_count; ++i) {
    buzzer_tone(pattern->steps[i].frequency_hz);
    HAL_Delay(pattern->steps[i].duration_ms);
  }
  buzzer_tone(BUZZER_TONE_SILENT_HZ);
}
```

- [ ] **Step 3: Correct the spec's stated bound**

In the spec, change "bounded at ≤ 400 ms" to "bounded at ≤ 500 ms" in decision §3.7, and "400 ms is imperceptible" to "480 ms at worst is imperceptible". In §7, add the measured totals after the table: `REQUEST_SENT` 160 ms, `DATA_RECEIVED` 150 ms, `PACKAGE_QUEUED` 220 ms, `LINK_FAULT` 480 ms, `BOUNDARY` 40 ms.

- [ ] **Step 4: Build and measure**

```bash
export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
cmake --build --preset Debug && cmake --build --preset Release
arm-none-eabi-size build/Release/single-cell-braille-controller.elf
make -C tests
```
Expected: both link, host suites still pass.

- [ ] **Step 5: Bench-verify the tones (hardware)**

This is the first task with something to hear, and it is worth proving before the link work buries it. Temporarily add to `App_init()` in `App/Src/app_main.c`, after `initialize_motors()`:

```c
  /* TEMPORARY bring-up check, removed in Task 12. */
  buzzer_init(&htim4, TIM_CHANNEL_1);
  buzzer_play(READER_SOUND_REQUEST_SENT);
  HAL_Delay(500u);
  buzzer_play(READER_SOUND_DATA_RECEIVED);
  HAL_Delay(500u);
  buzzer_play(READER_SOUND_PACKAGE_QUEUED);
  HAL_Delay(500u);
  buzzer_play(READER_SOUND_LINK_FAULT);
  HAL_Delay(500u);
  buzzer_play(READER_SOUND_BOUNDARY);
```

`app_main.c` needs `#include "buzzer.h"` and `#include "tim.h"` for `htim4`. Flash and listen: five patterns, each clearly distinct, no tone left sounding after the last one. Then **revert this block** — `git checkout App/Src/app_main.c` — before committing. Task 12 wires it properly.

If a tone sounds but the pitch is wrong, the counter tick is not 1 MHz: recheck `htim4.Init.Prescaler` (should be 47) and that APB1's timer clock is 48 MHz.

- [ ] **Step 6: Commit**

```bash
git add App/Inc/buzzer.h App/Src/buzzer.c docs/superpowers/specs/
git commit -m "feat: add the buzzer tone-pattern player

Five notification patterns on TIM4_CH1. The timer arrives from CubeMX with a
1 MHz counter tick, so pitch is just a period and the module only ever
rewrites ARR and CCR.

The patterns are separated by texture as much as by pitch, because two sounds
that differ only in frequency are hard to tell apart on a small piezo:
'asking' is a rising pair, 'received' is one longer tone, 'data is waiting' is
a fast triple chirp, and a fault is a slow low double -- deliberately the
longest pattern of the five.

buzzer_play() blocks, which is the one deliberate exception to the otherwise
cooperative design. PWM keeps sounding in hardware with no software
involvement, so a tone started just before a blocking disc move would sound
for the whole move unless something stopped it; blocking removes both the
truncation bug and the need for a tick. The worst pattern is 480 ms against
moves that already block for about a second, and presses stay latched by
their interrupt throughout.

The spec's claimed 400 ms bound was wrong for the fault pattern and is
corrected to 500 ms rather than shortening a tone that benefits from being
slow.

buzzer.h takes reader_sound_t rather than defining a parallel enum: those five
sounds are the device's whole audible vocabulary and there is no second
caller to justify a mapping table.

Release text: <record arm-none-eabi-size output here>.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: `buttons` — debounced navigation events

Spec §6 (button coalescing). All three buttons are falling-edge with pull-up on the shared `EXTI9_5_IRQn`, already configured by `MX_GPIO_Init()`.

**Files:**
- Create: `App/Inc/buttons.h`, `App/Src/buttons.c`

**Interfaces:**
- Consumes: `reader_event_t` (Task 6), `PLC_BUTTON_DEBOUNCE_LOCKOUT_MS` (Task 3), the `BTN_*_Pin` macros from `Core/Inc/main.h`.
- Produces: `void buttons_init(void)`, `void buttons_on_exti(uint16_t gpio_pin)`, `reader_event_t buttons_take_event(void)`.

- [ ] **Step 1: Create the header**

```c
/**
 * @file    buttons.h
 * @brief   Debounced NEXT / PREV / REPEAT presses.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * The three buttons share one EXTI line group, so the interrupt only records
 * that a press happened and the main loop collects it. Each button holds a
 * single flag rather than a count, which is what makes an impatient reader's
 * repeated taps during a disc move collapse into one advance instead of
 * skipping fields.
 */

#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

#include "reader_ui.h"

/**
 * @defgroup buttons Buttons
 * @brief    Debounced navigation input.
 * @{
 */

/** @brief Clear all pending presses and debounce state. */
void buttons_init(void);

/**
 * @brief Record a press from the EXTI callback.
 *
 * Safe to call from interrupt context, and cheap: it compares one timestamp
 * and sets one flag. Pins that do not belong to a button are ignored, so the
 * shared handler can hand it every line.
 *
 * @param gpio_pin The pin whose EXTI fired.
 */
void buttons_on_exti(uint16_t gpio_pin);

/**
 * @brief Take the next pending press, if any.
 *
 * Call from the main loop. When more than one button is pending, NEXT wins,
 * then PREV, then REPEAT -- the reader is far more likely to be moving
 * forward than to have meant a simultaneous press.
 *
 * @return The press, or @c READER_EV_NONE when nothing is pending.
 */
reader_event_t buttons_take_event(void);

/** @} */ // end of buttons

#endif // BUTTONS_H
```

- [ ] **Step 2: Create the implementation**

```c
// ----------------------------------------------------------------------------
// buttons.c
//
// Debouncing is "first edge wins, then lock out": the press registers on the
// very first edge, and further edges within the lockout window are ignored as
// contact bounce. That gives zero perceived latency, which matters for a
// control the reader operates by feel, and it needs no polling.
//
// Each button carries one flag, not a counter. A disc move blocks for about a
// second, and a reader who taps NEXT three times during one wants to move on
// by one field, not three.
// ----------------------------------------------------------------------------

#include "buttons.h"

#include "main.h"
#include "plc_config.h"
#include "stm32f1xx_hal.h"

typedef struct {
  uint16_t pin;
  reader_event_t event;
  volatile bool pending;
  volatile uint32_t last_accepted_ms;
  volatile bool ever_accepted;
} button_t;

// Collection order is the priority order: NEXT, then PREV, then REPEAT.
static button_t buttons[] = {
    {BTN_NEXT_Pin, READER_EV_NEXT, false, 0u, false},
    {BTN_PREV_Pin, READER_EV_PREV, false, 0u, false},
    {BTN_REPEAT_Pin, READER_EV_REPEAT, false, 0u, false},
};

#define BUTTON_COUNT (sizeof buttons / sizeof buttons[0])

void buttons_init(void) {
  for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
    buttons[i].pending = false;
    buttons[i].last_accepted_ms = 0u;
    buttons[i].ever_accepted = false;
  }
}

void buttons_on_exti(uint16_t gpio_pin) {
  uint32_t now = HAL_GetTick();
  for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
    if (buttons[i].pin != gpio_pin) {
      continue;
    }
    // Unsigned subtraction, so the comparison stays correct across a wrap of
    // the millisecond counter.
    bool settled = !buttons[i].ever_accepted ||
                   (uint32_t)(now - buttons[i].last_accepted_ms) >=
                       PLC_BUTTON_DEBOUNCE_LOCKOUT_MS;
    if (settled) {
      buttons[i].pending = true;
      buttons[i].last_accepted_ms = now;
      buttons[i].ever_accepted = true;
    }
    return;
  }
}

// Read-and-clear has to be indivisible: an EXTI landing between the test and
// the clear would otherwise have its press dropped. PRIMASK is saved and
// restored rather than blindly re-enabled, so this is safe to call from a
// context that already had interrupts masked.
static bool button_take(volatile bool *pending) {
  bool taken = false;
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  if (*pending) {
    *pending = false;
    taken = true;
  }
  if (primask == 0u) {
    __enable_irq();
  }
  return taken;
}

reader_event_t buttons_take_event(void) {
  for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
    if (button_take(&buttons[i].pending)) {
      return buttons[i].event;
    }
  }
  return READER_EV_NONE;
}
```

`buttons.c` uses `bool`, so add `#include <stdbool.h>` to `buttons.h`.

- [ ] **Step 3: Build**

```bash
export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
cmake --build --preset Debug && cmake --build --preset Release
arm-none-eabi-size build/Release/single-cell-braille-controller.elf
```

- [ ] **Step 4: Bench-verify the buttons (hardware)**

Nothing calls `HAL_GPIO_EXTI_Callback` yet, so add a temporary one at the end of `App/Src/app_main.c` plus a temporary drain in `App_run()`:

```c
/* TEMPORARY bring-up check, replaced in Task 12. */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) { buttons_on_exti(GPIO_Pin); }
```

and in `App_run()`, before anything else:

```c
  /* TEMPORARY: one buzzer pattern per button, to prove the wiring. */
  switch (buttons_take_event()) {
  case READER_EV_NEXT:   buzzer_play(READER_SOUND_DATA_RECEIVED); break;
  case READER_EV_PREV:   buzzer_play(READER_SOUND_BOUNDARY); break;
  case READER_EV_REPEAT: buzzer_play(READER_SOUND_PACKAGE_QUEUED); break;
  default: break;
  }
```

Flash, then press each button: exactly one sound per press, the right sound for each button, and no double-triggering from bounce. Hold a button down — it must sound once, not repeatedly, since only the falling edge is configured. Then **revert both blocks** (`git checkout App/Src/app_main.c`) before committing.

If a button gives two sounds per press, raise `PLC_BUTTON_DEBOUNCE_LOCKOUT_MS`. If a button does nothing, confirm its pin in `Core/Inc/main.h` (`BTN_NEXT_Pin` PB8, `BTN_PREV_Pin` PB9, `BTN_REPEAT_Pin` PB7) and that `EXTI9_5_IRQHandler` in `Core/Src/stm32f1xx_it.c` forwards that pin.

- [ ] **Step 5: Commit**

```bash
git add App/Inc/buttons.h App/Src/buttons.c
git commit -m "feat: add debounced navigation buttons

The three buttons share one EXTI group, so the interrupt only records that a
press happened and the main loop collects it.

Debouncing is 'first edge wins, then lock out' rather than a settle-and-poll:
the press registers on the very first edge, which matters for a control the
reader works by feel, and further edges inside the 40 ms window are discarded
as contact bounce. No polling loop is needed.

Each button holds one flag rather than a count, deliberately. A disc move
blocks for roughly a second, and a reader who taps NEXT three times during one
means 'move on', not 'skip three fields'.

Read-and-clear masks interrupts around the test-and-clear pair, since an EXTI
landing between them would drop the press. PRIMASK is saved and restored
instead of unconditionally re-enabled, so the function stays safe to call from
an already-masked context.

Release text: <record arm-none-eabi-size output here>.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: Vendor ioLibrary and port it to SPI2

Spec §4.1, §8. The first task that can fail for reasons outside the code, so it ends by proving the SPI link against a known register value rather than by "it compiles".

**Files:**
- Create: `Lib/w5500/CMakeLists.txt`, `Lib/w5500/UPSTREAM.md`, `Lib/w5500/w5500_stm32.h`, `Lib/w5500/w5500_stm32.c`, `Lib/w5500/ioLibrary/**`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `hspi2` (from `Core/Inc/spi.h`), the `ETH_*` pin macros from `Core/Inc/main.h`, `PLC_W5500_RESET_LOW_MS`/`PLC_W5500_BOOT_MS` (Task 3).
- Produces: `void w5500_stm32_init(SPI_HandleTypeDef *hspi)` (registers all ioLibrary callbacks), `void w5500_stm32_hard_reset(void)`, plus the whole ioLibrary API (`wizchip_init`, `wizchip_setnetinfo`, `getVERSIONR`, `wizphy_getphylink`, `socket`, `connect`, `send`, `recv`, `close`, `getSn_SR`, `getSn_RX_RSR`).

- [ ] **Step 1: Fetch upstream and copy in only the Ethernet tree**

```bash
git clone --depth 1 https://github.com/Wiznet/ioLibrary_Driver /tmp/ioLibrary_Driver
mkdir -p Lib/w5500/ioLibrary/Ethernet/W5500
cp /tmp/ioLibrary_Driver/Ethernet/wizchip_conf.c Lib/w5500/ioLibrary/Ethernet/
cp /tmp/ioLibrary_Driver/Ethernet/wizchip_conf.h Lib/w5500/ioLibrary/Ethernet/
cp /tmp/ioLibrary_Driver/Ethernet/socket.c       Lib/w5500/ioLibrary/Ethernet/
cp /tmp/ioLibrary_Driver/Ethernet/socket.h       Lib/w5500/ioLibrary/Ethernet/
cp /tmp/ioLibrary_Driver/Ethernet/W5500/w5500.c  Lib/w5500/ioLibrary/Ethernet/W5500/
cp /tmp/ioLibrary_Driver/Ethernet/W5500/w5500.h  Lib/w5500/ioLibrary/Ethernet/W5500/
cp /tmp/ioLibrary_Driver/LICENSE                 Lib/w5500/ioLibrary/ 2>/dev/null || \
  cp /tmp/ioLibrary_Driver/license.txt           Lib/w5500/ioLibrary/
git -C /tmp/ioLibrary_Driver rev-parse HEAD
```

Deliberately **not** copied: the whole `Internet/` tree (DHCP, DNS, SNTP, HTTP, FTP, MQTT) and the other chip directories. Nothing here needs them, and the design chose static addressing precisely so `dhcp.c` never has to exist on this part.

- [ ] **Step 2: Configure the chip selection and record every upstream change**

Open `Lib/w5500/ioLibrary/Ethernet/wizchip_conf.h` and find the `_WIZCHIP_` and `_WIZCHIP_IO_MODE_` definitions. Two things must be true: `_WIZCHIP_` selects `W5500`, and the I/O mode is `_WIZCHIP_IO_MODE_SPI_VDM_` (variable-length data mode, the W5500's normal SPI framing).

Prefer setting these from CMake so the vendored source stays pristine. If upstream defines `_WIZCHIP_` unconditionally, the minimal change is to wrap it:

```c
#ifndef _WIZCHIP_
#define _WIZCHIP_ W5500
#endif
```

Whatever you end up doing, write it down. Create `Lib/w5500/UPSTREAM.md`:

```markdown
# Vendored WIZnet ioLibrary_Driver

Source: https://github.com/Wiznet/ioLibrary_Driver
Commit: <paste the rev-parse output from step 1>
Fetched: 2026-09-09

## What was copied

Only `Ethernet/`:

- `Ethernet/wizchip_conf.{c,h}`
- `Ethernet/socket.{c,h}`
- `Ethernet/W5500/w5500.{c,h}`
- the upstream licence file

The `Internet/` tree is deliberately absent. This device has a static address
and speaks only MODBUS TCP, so DHCP, DNS, SNTP, HTTP and FTP would be dead
code on a 64 KB part.

## Local modifications

<List every edit, or "none". If wizchip_conf.h's _WIZCHIP_ define was
wrapped in #ifndef, say so here -- that is the difference that will confuse
whoever next compares this tree against upstream.>

## Configuration

`_WIZCHIP_` = W5500 and `_WIZCHIP_IO_MODE_` = `_WIZCHIP_IO_MODE_SPI_VDM_`,
set in `Lib/w5500/CMakeLists.txt` where possible.
```

- [ ] **Step 3: Create the library target**

`Lib/w5500/CMakeLists.txt`, following `Lib/tmc2209/CMakeLists.txt`:

```cmake
add_library(w5500 STATIC
    ioLibrary/Ethernet/wizchip_conf.c
    ioLibrary/Ethernet/socket.c
    ioLibrary/Ethernet/W5500/w5500.c
    w5500_stm32.c
)

target_include_directories(w5500 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ioLibrary/Ethernet
    ${CMAKE_CURRENT_SOURCE_DIR}/ioLibrary/Ethernet/W5500
)

# Chip and SPI framing selection. PUBLIC so a consumer including
# wizchip_conf.h sees the same chip as the library was built for -- a mismatch
# here silently changes register offsets.
target_compile_definitions(w5500 PUBLIC
    _WIZCHIP_=W5500
    _WIZCHIP_IO_MODE_=_WIZCHIP_IO_MODE_SPI_VDM_
)

# The STM32 port needs the App config header for its reset timings, and
# Core/Inc for the ETH_* pin macros.
target_include_directories(w5500 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../App/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc
)

# Interface target from cmake/stm32cubemx providing the CMSIS/HAL include
# paths and the STM32F103xB device define.
target_link_libraries(w5500 PRIVATE stm32cubemx)

# Vendored third-party code: do not let its warnings fail our build, but do
# not silence warnings in our own port either.
set_source_files_properties(
    ioLibrary/Ethernet/wizchip_conf.c
    ioLibrary/Ethernet/socket.c
    ioLibrary/Ethernet/W5500/w5500.c
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare"
)
```

In the top-level `CMakeLists.txt`, after `add_subdirectory(Lib/tmc2209)`:

```cmake
add_subdirectory(Lib/w5500)
```

and add `w5500` to `target_link_libraries(${CMAKE_PROJECT_NAME} ...)` next to `tmc2209`.

- [ ] **Step 4: Create the port header**

```c
/**
 * @file    w5500_stm32.h
 * @brief   STM32 SPI port for the vendored WIZnet ioLibrary.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * ioLibrary reaches hardware through callbacks the application registers.
 * This is that registration plus the four things it needs: chip select,
 * single-byte and burst SPI transfers, and the hard reset line.
 */

#ifndef W5500_STM32_H
#define W5500_STM32_H

#include "stm32f1xx_hal.h"

/**
 * @defgroup w5500_stm32 W5500 STM32 Port
 * @brief    SPI and reset glue for ioLibrary.
 * @{
 */

/**
 * @brief Register every ioLibrary callback against an SPI peripheral.
 *
 * Call once, before @c wizchip_init(). Registers chip select, single-byte
 * read/write, burst read/write, and the critical-section pair.
 *
 * @param hspi SPI peripheral wired to the W5500 (SPI2 on this board).
 */
void w5500_stm32_init(SPI_HandleTypeDef *hspi);

/**
 * @brief Pulse the W5500's reset line and wait for it to come back.
 *
 * Blocking, for about 12 ms. It runs at bring-up and on a reconnect attempt
 * -- never while the reader is mid-package -- so a timed state machine here
 * would be complexity with nothing to show for it.
 */
void w5500_stm32_hard_reset(void);

/** @} */ // end of w5500_stm32

#endif // W5500_STM32_H
```

- [ ] **Step 5: Create the port implementation**

```c
// ----------------------------------------------------------------------------
// w5500_stm32.c
//
// ioLibrary calls out through function pointers; this file supplies them for
// SPI2 with a software-driven chip select on PB12.
//
// Burst callbacks are registered as well as the single-byte pair, because
// every register access ioLibrary makes is a burst underneath: without them
// each byte of a 16-byte read would be its own HAL call.
// ----------------------------------------------------------------------------

#include "w5500_stm32.h"

#include "main.h"
#include "plc_config.h"
#include "wizchip_conf.h"

/** @brief Clocked out while reading, since the bus is full duplex. */
#define W5500_SPI_DUMMY_BYTE 0xFFu

/**
 * @brief Per-transfer SPI timeout.
 *
 * At 12 Mbit/s the largest transfer this driver makes is far under a
 * millisecond, so anything approaching this bound means the bus is stuck
 * rather than busy.
 */
#define W5500_SPI_TIMEOUT_MS 100u

static SPI_HandleTypeDef *w5500_spi;

static void w5500_cs_select(void) {
  HAL_GPIO_WritePin(ETH_NSS_GPIO_Port, ETH_NSS_Pin, GPIO_PIN_RESET);
}

static void w5500_cs_deselect(void) {
  HAL_GPIO_WritePin(ETH_NSS_GPIO_Port, ETH_NSS_Pin, GPIO_PIN_SET);
}

static void w5500_write_burst(uint8_t *buffer, uint16_t length) {
  (void)HAL_SPI_Transmit(w5500_spi, buffer, length, W5500_SPI_TIMEOUT_MS);
}

static void w5500_read_burst(uint8_t *buffer, uint16_t length) {
  // Full duplex: bytes only come back if something is clocked out. Filling
  // the buffer with the dummy byte and using it as both source and
  // destination is what HAL_SPI_Receive does internally on this family,
  // written out here so the dummy traffic is visible rather than implied.
  for (uint16_t i = 0u; i < length; ++i) {
    buffer[i] = W5500_SPI_DUMMY_BYTE;
  }
  (void)HAL_SPI_TransmitReceive(w5500_spi, buffer, buffer, length,
                                W5500_SPI_TIMEOUT_MS);
}

static void w5500_write_byte(uint8_t data) { w5500_write_burst(&data, 1u); }

static uint8_t w5500_read_byte(void) {
  uint8_t data = W5500_SPI_DUMMY_BYTE;
  w5500_read_burst(&data, 1u);
  return data;
}

// ioLibrary wraps each chip access in these so an application that touches
// SPI from more than one context can serialise it. Here SPI2 is driven only
// from plc_link_tick(), which runs in the main loop; no interrupt on this
// board touches the bus. Masking interrupts anyway would add jitter to the
// stepper's STEP timers for no benefit.
static void w5500_cris_enter(void) {}
static void w5500_cris_exit(void) {}

void w5500_stm32_init(SPI_HandleTypeDef *hspi) {
  w5500_spi = hspi;
  w5500_cs_deselect();

  reg_wizchip_cris_cbfunc(w5500_cris_enter, w5500_cris_exit);
  reg_wizchip_cs_cbfunc(w5500_cs_select, w5500_cs_deselect);
  reg_wizchip_spi_cbfunc(w5500_read_byte, w5500_write_byte);
  reg_wizchip_spiburst_cbfunc(w5500_read_burst, w5500_write_burst);
}

void w5500_stm32_hard_reset(void) {
  HAL_GPIO_WritePin(ETH_RESET_GPIO_Port, ETH_RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(PLC_W5500_RESET_LOW_MS);
  HAL_GPIO_WritePin(ETH_RESET_GPIO_Port, ETH_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(PLC_W5500_BOOT_MS);
}
```

If `reg_wizchip_spiburst_cbfunc` does not exist in the fetched version, or its callbacks take `(uint8_t*, datasize_t)`, match the local signatures to the header and note the difference in `UPSTREAM.md`.

- [ ] **Step 6: Build**

```bash
export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
cmake --preset Debug && cmake --build --preset Debug
cmake --preset Release && cmake --build --preset Release
arm-none-eabi-size build/Debug/single-cell-braille-controller.elf
arm-none-eabi-size build/Release/single-cell-braille-controller.elf
```
Reconfiguring is required — a new `add_subdirectory` is not picked up by a plain rebuild. Nothing calls into `w5500` yet, so `--gc-sections` will drop most of it; the meaningful measurement comes in Task 10.

- [ ] **Step 7: Prove the SPI link on hardware**

This is the step that matters. `VERSIONR` on a W5500 always reads `0x04`, so it is a perfect end-to-end check of chip select, clock polarity, bit order and wiring — all at once, before any of it is buried under a state machine.

Temporarily add to `App_init()` in `App/Src/app_main.c`:

```c
  /* TEMPORARY bring-up check, removed in Task 12. VERSIONR is 0x04 on every
   * W5500, so this proves the SPI path rather than just the compile. */
  w5500_stm32_hard_reset();
  w5500_stm32_init(&hspi2);
  if (getVERSIONR() == 0x04u) {
    buzzer_play(READER_SOUND_DATA_RECEIVED); /* SPI is talking */
  } else {
    fault_led_blink(FAULT_LED_CODE_ETHERNET);
    buzzer_play(READER_SOUND_LINK_FAULT);
  }
```

with `#include "w5500_stm32.h"`, `#include "wizchip_conf.h"`, `#include "buzzer.h"`, `#include "fault_led.h"` and `#include "spi.h"`.

Expected: the confirm tone. If you get the fault tone and three LED blinks, work through it in this order:

1. **All zeroes or all ones read back** — check `ETH_NSS` (PB12) actually toggles, and that the module has 3.3 V and a solid ground back to the board.
2. **Nothing at all** — confirm `ETH_RESET` (PA10) goes low then high; the W5500 stays held in reset otherwise.
3. **Garbled** — SPI2 is configured `CPOL=Low, CPHA=1Edge` (mode 0) and MSB-first, which is what the W5500 wants. Confirm nothing has changed that in `Core/Src/spi.c`.
4. **Intermittent** — drop `hspi2.Init.BaudRatePrescaler` from `SPI_BAUDRATEPRESCALER_2` (12 Mbit/s) to `_4` in CubeMX; long jumper leads do not carry 12 MHz well. Note it in `UPSTREAM.md` if you change it.

Then **revert the temporary block** before committing.

- [ ] **Step 8: Commit**

```bash
git add Lib/w5500 CMakeLists.txt
git commit -m "feat: vendor WIZnet ioLibrary and port it to SPI2

Only the Ethernet tree is copied: wizchip_conf, socket, and the W5500
register layer. The whole Internet/ tree is deliberately absent -- this device
has a static address and speaks nothing but MODBUS TCP, so DHCP, DNS, SNTP,
HTTP and FTP would be dead code on a 64 KB part. UPSTREAM.md records the
commit fetched and every local modification, because a vendored tree nobody
can diff against upstream turns into a fork by accident.

The port registers burst callbacks as well as the single-byte pair. Every
register access ioLibrary makes is a burst underneath, so without them each
byte of a multi-byte read would be a separate HAL call.

The critical-section callbacks are intentionally empty. SPI2 is driven only
from the main loop and no interrupt on this board touches the bus, so masking
interrupts around each access would add jitter to the stepper STEP timers for
no benefit -- documented in the source so it reads as a decision rather than
an omission.

_WIZCHIP_ and _WIZCHIP_IO_MODE_ are set PUBLIC on the target: a consumer that
included wizchip_conf.h with a different chip selected would compute different
register offsets and fail in a way that looks like a wiring problem.

Verified on hardware by reading VERSIONR as 0x04, which exercises chip select,
clock polarity, bit order and wiring together.

Release text: <record arm-none-eabi-size output here>.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 10: `plc_link` — connect, poll, recover

Spec §8, §10. Non-blocking except the ~12 ms reset, which only runs at bring-up and on a reconnect attempt.

**Files:**
- Create: `App/Inc/plc_link.h`, `App/Src/plc_link.c`

**Interfaces:**
- Consumes: `w5500_stm32_init()`, `w5500_stm32_hard_reset()`, the ioLibrary API (Task 9); `modbus_build_read_coils()`, `modbus_parse_read_coils()` (Task 5); `plc_snapshot_t` (Task 4); all of `plc_config.h` (Task 3); `fault_led_blink()` (Task 2).
- Produces: `plc_link_io_t` (`on_snapshot`, `on_fault`), `void plc_link_init(SPI_HandleTypeDef *hspi, const plc_link_io_t *io)`, `void plc_link_tick(void)`, `void plc_link_request_now(void)`, `void plc_link_on_exti(uint16_t gpio_pin)`.

- [ ] **Step 1: Create the header**

```c
/**
 * @file    plc_link.h
 * @brief   MODBUS TCP client link to the PLC over the W5500.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * Owns the W5500, the one TCP connection, and the polling cycle. The device
 * is always the client: MODBUS has no way for a server to push, so "the PLC
 * sent something" is really "a background poll came back different", and
 * detecting that is this module's job.
 *
 * Everything is driven from @ref plc_link_tick and returns immediately, with
 * one exception: bringing the chip out of reset blocks for about 12 ms. That
 * happens at startup and on a reconnect attempt, never while the reader is
 * mid-package.
 */

#ifndef PLC_LINK_H
#define PLC_LINK_H

#include <stdint.h>

#include "plc_packet.h"
#include "stm32f1xx_hal.h"

/**
 * @defgroup plc_link PLC Link
 * @brief    W5500 bring-up, TCP connection and MODBUS polling.
 * @{
 */

/** @brief Where the link reports what it found. */
typedef struct {
  /** @brief A package was read successfully. */
  void (*on_snapshot)(plc_snapshot_t snapshot);
  /** @brief The link could not deliver: no chip, no cable, no answer, or a
   *         response that failed validation. */
  void (*on_fault)(void);
} plc_link_io_t;

/**
 * @brief Bring up the W5500 and start trying to reach the PLC.
 *
 * Resets the chip, registers the SPI callbacks, allocates the whole packet
 * buffer to one socket and applies the static network identity from
 * plc_config.h. A chip that does not answer is reported with
 * @ref FAULT_LED_CODE_ETHERNET and retried from @ref plc_link_tick rather
 * than halting startup.
 *
 * @param hspi SPI peripheral wired to the W5500.
 * @param io   Callbacks. Must outlive the link; not copied.
 */
void plc_link_init(SPI_HandleTypeDef *hspi, const plc_link_io_t *io);

/**
 * @brief Advance the link by one step.
 *
 * Call once per main-loop pass. Connects if not connected, polls when the
 * interval has elapsed or a poll was requested, times out a request that goes
 * unanswered, and reconnects after a fault.
 */
void plc_link_tick(void);

/**
 * @brief Ask for a poll on the next tick instead of waiting for the interval.
 *
 * Used when the reader steps past the last field and needs fresh data now.
 * Ignored while a request is already in flight.
 */
void plc_link_request_now(void);

/**
 * @brief Note that the W5500 asserted its interrupt line.
 *
 * Safe to call from interrupt context; it only sets a flag. This is purely an
 * optimisation -- it lets the next tick service the socket without waiting
 * for the poll instant -- and the link works correctly if the pin never
 * fires.
 *
 * @param gpio_pin The pin whose EXTI fired; anything else is ignored.
 */
void plc_link_on_exti(uint16_t gpio_pin);

/** @} */ // end of plc_link

#endif // PLC_LINK_H
```

- [ ] **Step 2: Create the implementation**

```c
// ----------------------------------------------------------------------------
// plc_link.c
//
// The client side of the link, as a state machine ticked from the main loop.
//
// The device polls continuously rather than waiting to be told anything,
// because MODBUS gives a server no way to push to a client. That is the whole
// mechanism behind "the PLC sent something unrequested": a background poll
// came back different from what the reader is holding, and the reader is
// offered the newer package.
// ----------------------------------------------------------------------------

#include "plc_link.h"

#include <stdbool.h>
#include <stddef.h>

#include "fault_led.h"
#include "main.h"
#include "modbus_tcp.h"
#include "plc_config.h"
#include "socket.h"
#include "w5500_stm32.h"
#include "wizchip_conf.h"

/** @brief W5500's VERSIONR always reads this; anything else is not a W5500. */
#define PLC_W5500_VERSION 0x04u

/**
 * @brief Socket buffer allocation, in kilobytes per socket.
 *
 * The W5500 has 16 KB of TX and 16 KB of RX to divide among eight sockets.
 * Only one connection is ever opened, so it takes all of it: a 12-byte
 * request and an 11-byte response could live in the smallest allocation, but
 * there is nothing else to spend it on.
 */
#define PLC_SOCKET_BUFFER_KB 16u

/** @brief Largest frame either direction, so one buffer serves both. */
#define PLC_FRAME_BUFFER_LEN 32u

typedef enum {
  PLC_LINK_CHIP_FAULT = 0,   /**< No usable W5500; retry bring-up.       */
  PLC_LINK_LINK_WAIT,        /**< Chip is up, waiting for PHY link.      */
  PLC_LINK_CONNECTING,       /**< Socket open, connect in progress.      */
  PLC_LINK_IDLE,             /**< Connected, waiting for the next poll.  */
  PLC_LINK_AWAITING_RESPONSE,/**< Request sent, reply outstanding.       */
  PLC_LINK_BACKOFF,          /**< Socket closed, pausing before a retry. */
} plc_link_state_t;

static struct {
  const plc_link_io_t *io;
  SPI_HandleTypeDef *spi;
  plc_link_state_t state;
  uint32_t state_entered_ms;
  uint32_t last_poll_ms;
  bool poll_requested;
  uint16_t transaction_id;
  volatile bool irq_pending;
  uint8_t frame[PLC_FRAME_BUFFER_LEN];
} link;

static void enter(plc_link_state_t state) {
  link.state = state;
  link.state_entered_ms = HAL_GetTick();
}

// Unsigned subtraction, so elapsed time stays correct across a wrap.
static bool elapsed_since(uint32_t since_ms, uint32_t interval_ms) {
  return (uint32_t)(HAL_GetTick() - since_ms) >= interval_ms;
}

static void report_fault(void) {
  if (link.io != NULL && link.io->on_fault != NULL) {
    link.io->on_fault();
  }
}

// Close the socket and pause. Everything that goes wrong lands here, so the
// recovery path is exercised by every failure rather than only by the one the
// author thought of.
static void fail_to_backoff(void) {
  (void)close(PLC_SOCKET_NUMBER);
  report_fault();
  enter(PLC_LINK_BACKOFF);
}

// Reset the chip and apply our identity. Returns false when the W5500 does
// not answer at all, which is a wiring or power problem rather than a network
// one.
static bool chip_bring_up(void) {
  static const uint8_t tx_sizes[8] = {PLC_SOCKET_BUFFER_KB, 0u, 0u, 0u,
                                      0u,                   0u, 0u, 0u};
  static const uint8_t rx_sizes[8] = {PLC_SOCKET_BUFFER_KB, 0u, 0u, 0u,
                                      0u,                   0u, 0u, 0u};

  w5500_stm32_hard_reset();
  w5500_stm32_init(link.spi);

  if (getVERSIONR() != PLC_W5500_VERSION) {
    return false;
  }
  if (wizchip_init((uint8_t *)tx_sizes, (uint8_t *)rx_sizes) != 0) {
    return false;
  }

  wiz_NetInfo netinfo = {
      .mac = {PLC_DEVICE_MAC_0, PLC_DEVICE_MAC_1, PLC_DEVICE_MAC_2,
              PLC_DEVICE_MAC_3, PLC_DEVICE_MAC_4, PLC_DEVICE_MAC_5},
      .ip = {PLC_DEVICE_IP_0, PLC_DEVICE_IP_1, PLC_DEVICE_IP_2,
             PLC_DEVICE_IP_3},
      .sn = {PLC_DEVICE_MASK_0, PLC_DEVICE_MASK_1, PLC_DEVICE_MASK_2,
             PLC_DEVICE_MASK_3},
      .gw = {PLC_GATEWAY_0, PLC_GATEWAY_1, PLC_GATEWAY_2, PLC_GATEWAY_3},
      .dns = {PLC_GATEWAY_0, PLC_GATEWAY_1, PLC_GATEWAY_2, PLC_GATEWAY_3},
      .dhcp = NETINFO_STATIC,
  };
  wizchip_setnetinfo(&netinfo);
  return true;
}

static void send_request(void) {
  ++link.transaction_id; // wraps; the parser only needs it to differ
  uint16_t length = modbus_build_read_coils(
      link.frame, (uint16_t)sizeof link.frame, link.transaction_id,
      PLC_UNIT_ID, PLC_COIL_BASE_ADDRESS, PLC_COIL_COUNT);
  if (length == 0u) {
    fail_to_backoff();
    return;
  }
  if (send(PLC_SOCKET_NUMBER, link.frame, (uint16_t)length) != (int32_t)length) {
    fail_to_backoff();
    return;
  }
  link.poll_requested = false;
  link.last_poll_ms = HAL_GetTick();
  enter(PLC_LINK_AWAITING_RESPONSE);
}

static void read_response(void) {
  uint16_t available = getSn_RX_RSR(PLC_SOCKET_NUMBER);
  if (available < MODBUS_READ_COILS_RSP_LEN(PLC_COIL_COUNT)) {
    return; // still arriving; the timeout in the caller bounds the wait
  }
  if (available > (uint16_t)sizeof link.frame) {
    available = (uint16_t)sizeof link.frame;
  }

  int32_t received = recv(PLC_SOCKET_NUMBER, link.frame, available);
  if (received <= 0) {
    fail_to_backoff();
    return;
  }

  uint16_t bits = 0u;
  uint8_t exception = 0u;
  modbus_status_t status = modbus_parse_read_coils(
      link.frame, (uint16_t)received, link.transaction_id, PLC_UNIT_ID,
      PLC_COIL_COUNT, &bits, &exception);

  if (status != MODBUS_OK) {
    // A rejected frame is a fault, not a snapshot: handing the reader values
    // from a frame we could not validate is worse than telling them the link
    // is unhappy. The connection itself is still good, so go back to idle
    // rather than tearing it down.
    report_fault();
    enter(PLC_LINK_IDLE);
    return;
  }

  plc_snapshot_t snapshot = {bits};
  enter(PLC_LINK_IDLE);
  if (link.io != NULL && link.io->on_snapshot != NULL) {
    link.io->on_snapshot(snapshot);
  }
}

void plc_link_init(SPI_HandleTypeDef *hspi, const plc_link_io_t *io) {
  link.io = io;
  link.spi = hspi;
  link.poll_requested = false;
  link.transaction_id = 0u;
  link.irq_pending = false;
  link.last_poll_ms = HAL_GetTick();

  if (!chip_bring_up()) {
    // Not fatal: the app still starts so the discs and buttons can be used,
    // and the LED says which subsystem is missing.
    fault_led_blink(FAULT_LED_CODE_ETHERNET);
    report_fault();
    enter(PLC_LINK_CHIP_FAULT);
    return;
  }
  enter(PLC_LINK_LINK_WAIT);
}

void plc_link_request_now(void) {
  if (link.state != PLC_LINK_AWAITING_RESPONSE) {
    link.poll_requested = true;
  }
}

void plc_link_on_exti(uint16_t gpio_pin) {
  if (gpio_pin == ETH_INT_Pin) {
    link.irq_pending = true;
  }
}

void plc_link_tick(void) {
  switch (link.state) {
  case PLC_LINK_CHIP_FAULT:
    if (elapsed_since(link.state_entered_ms, PLC_RECONNECT_DELAY_MS)) {
      if (chip_bring_up()) {
        enter(PLC_LINK_LINK_WAIT);
      } else {
        enter(PLC_LINK_CHIP_FAULT); // restart the delay, stay quiet
      }
    }
    return;

  case PLC_LINK_LINK_WAIT:
    // An unplugged cable sits here indefinitely, which is correct: there is
    // nothing to retry and the reader has already been told once.
    if (wizphy_getphylink() == PHY_LINK_ON) {
      if (socket(PLC_SOCKET_NUMBER, Sn_MR_TCP, PLC_LOCAL_PORT,
                 SF_IO_NONBLOCK) != (int8_t)PLC_SOCKET_NUMBER) {
        fail_to_backoff();
        return;
      }
      static const uint8_t server_ip[4] = {PLC_SERVER_IP_0, PLC_SERVER_IP_1,
                                           PLC_SERVER_IP_2, PLC_SERVER_IP_3};
      // Non-blocking, so SOCK_BUSY here means "in progress", not "failed".
      (void)connect(PLC_SOCKET_NUMBER, (uint8_t *)server_ip, PLC_SERVER_PORT);
      enter(PLC_LINK_CONNECTING);
    }
    return;

  case PLC_LINK_CONNECTING:
    switch (getSn_SR(PLC_SOCKET_NUMBER)) {
    case SOCK_ESTABLISHED:
      link.last_poll_ms = HAL_GetTick();
      enter(PLC_LINK_IDLE);
      return;
    case SOCK_CLOSED:
      fail_to_backoff();
      return;
    default:
      if (elapsed_since(link.state_entered_ms, PLC_RESPONSE_TIMEOUT_MS)) {
        fail_to_backoff();
      }
      return;
    }

  case PLC_LINK_IDLE:
    if (getSn_SR(PLC_SOCKET_NUMBER) != SOCK_ESTABLISHED) {
      fail_to_backoff();
      return;
    }
    if (link.poll_requested ||
        elapsed_since(link.last_poll_ms, PLC_POLL_INTERVAL_MS)) {
      send_request();
    }
    return;

  case PLC_LINK_AWAITING_RESPONSE:
    link.irq_pending = false; // the flag only exists to skip the wait below
    if (getSn_SR(PLC_SOCKET_NUMBER) != SOCK_ESTABLISHED) {
      fail_to_backoff();
      return;
    }
    read_response();
    if (link.state == PLC_LINK_AWAITING_RESPONSE &&
        elapsed_since(link.state_entered_ms, PLC_RESPONSE_TIMEOUT_MS)) {
      // The connection may well be fine and the PLC merely slow, but a reader
      // waiting on a value needs to be told something, and the next poll will
      // try again.
      report_fault();
      enter(PLC_LINK_IDLE);
    }
    return;

  case PLC_LINK_BACKOFF:
    if (elapsed_since(link.state_entered_ms, PLC_RECONNECT_DELAY_MS)) {
      enter(PLC_LINK_LINK_WAIT);
    }
    return;

  default:
    enter(PLC_LINK_BACKOFF);
    return;
  }
}
```

Two things to check against the fetched ioLibrary while implementing: `wizchip_init()`'s return convention (0 or greater on success in current versions) and whether `wizphy_getphylink()` is present — on some releases the PHY helpers sit behind `_WIZCHIP_ == W5500`. If it is missing, read `PHYCFGR` directly with `getPHYCFGR() & PHYCFGR_LNK_ON`.

- [ ] **Step 3: Build and measure**

```bash
export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
cmake --build --preset Debug && cmake --build --preset Release
arm-none-eabi-size build/Debug/single-cell-braille-controller.elf
arm-none-eabi-size build/Release/single-cell-braille-controller.elf
```
This is the task that reveals ioLibrary's real linked cost, since `plc_link` is the first thing to reference `socket()` and `wizchip_*`. Expect Release text to rise by roughly 5–8 KB. Record both numbers; if Debug approaches 64 KB, Task 11 is the relief and can be pulled forward.

- [ ] **Step 4: Commit**

```bash
git add App/Inc/plc_link.h App/Src/plc_link.c
git commit -m "feat: add the MODBUS TCP link state machine

The device polls continuously as a client, because MODBUS gives a server no
way to push. That is the whole mechanism behind 'the PLC sent something
unrequested': a background poll comes back different from what the reader is
holding, and the newer package is offered to them.

Everything is driven from plc_link_tick() and returns immediately, with one
exception: bringing the chip out of reset blocks for about 12 ms. It runs at
startup and on a reconnect attempt, never while the reader is mid-package, so
a timed state machine there would be complexity with nothing to show for it.

Every failure funnels through one fail_to_backoff() path, so the recovery
route is exercised by whatever actually goes wrong rather than only by the
case the author had in mind. Two failures are handled differently on purpose:
a response that fails validation and a request that times out are faults but
not connection problems, so the socket stays up and the next poll retries --
tearing down a working connection because one frame was bad would turn a
transient into an outage.

A W5500 that does not answer is reported with blink code 3 and retried from
the tick rather than halting startup, matching how a motor that fails to home
is handled: the rest of the device still works and the LED says what is
missing.

ETH_INT only sets a flag that lets the next tick service the socket early.
The poll-interval path is correct on its own, so the optimisation degrades
cleanly if the pin never fires.

Release text: <record arm-none-eabi-size output here>.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Phase 3 — Build variants, integration, documentation

### Task 11: The two input variants

Spec decision §3.6. `BRAILLE_INPUT_MODBUS` is the default; `BRAILLE_INPUT_USB_CDC` rebuilds today's behaviour.

**The constraint that shapes this task.** `MX_USB_DEVICE_Init()` is called from generated `main()` and `Core/Src/stm32f1xx_it.c:213` calls `HAL_PCD_IRQHandler(&hpcd_USB_FS)`. Both sit outside any `USER CODE` region, so neither can be guarded — CubeMX would overwrite the guard. The vector table keeps that ISR alive through `--gc-sections`, so excluding the USB sources leaves two unresolved symbols. The fix is to supply no-op definitions, which means the reachable part of `stm32f1xx_hal_pcd.c` and `stm32f1xx_ll_usb.c` stays linked. That is a known, accepted cost — Release has 43 KB free, and this task is about build hygiene, not about making the feature fit.

**Files:**
- Create: `App/Src/usb_device_stub.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: the `BRAILLE_INPUT_MODBUS` / `BRAILLE_INPUT_USB_CDC` compile definitions, and a `BRAILLE_INPUT` CMake cache variable.

- [ ] **Step 1: Create the stub**

The file is always compiled — `App/Src/*.c` is globbed — so its contents are guarded rather than its compilation.

```c
// ----------------------------------------------------------------------------
// usb_device_stub.c
//
// The MODBUS variant excludes USB_DEVICE/ from the build, but two references
// to it survive in generated code that cannot be guarded:
//
//   * Core/Src/main.c calls MX_USB_DEVICE_Init() from the init sequence.
//   * Core/Src/stm32f1xx_it.c's USB_LP_CAN1_RX0_IRQHandler calls
//     HAL_PCD_IRQHandler(&hpcd_USB_FS), and the vector table keeps that
//     handler alive through --gc-sections.
//
// Both sit outside any USER CODE region, so guarding them there would be
// undone by the next CubeMX code generation. No-op definitions here satisfy
// the link instead, and leave the generated files untouched.
//
// The handler can never actually run: nothing enables the USB interrupt in
// this variant, because MX_USB_DEVICE_Init() is the no-op below.
// ----------------------------------------------------------------------------

#if defined(BRAILLE_INPUT_MODBUS)

#include "stm32f1xx_hal.h"

/** @brief Stands in for USB_DEVICE/App/usb_device.c's initialiser. */
void MX_USB_DEVICE_Init(void) {}

/** @brief Stands in for USB_DEVICE/Target/usbd_conf.c's PCD handle. */
PCD_HandleTypeDef hpcd_USB_FS;

#endif // BRAILLE_INPUT_MODBUS
```

- [ ] **Step 2: Add the variant switch to the top-level CMakeLists.txt**

Append to the **end** of `CMakeLists.txt`, after `target_link_libraries()`. It must come after `add_subdirectory(cmake/stm32cubemx)` so the generated sources are already attached to the target.

```cmake
# ---------------------------------------------------------------------------
# Input source variant
#
# The MODBUS reader is the product. The USB typed-character path is kept for
# bench work and for regression-testing the braille rendering without a PLC,
# and is selected with -DBRAILLE_INPUT=USB_CDC at configure time.
#
# The surgery below lives here rather than in cmake/stm32cubemx/CMakeLists.txt
# because that file is regenerated by CubeMX; this one is generated once and
# is ours to edit.
# ---------------------------------------------------------------------------
set(BRAILLE_INPUT "MODBUS" CACHE STRING "Braille input source")
set_property(CACHE BRAILLE_INPUT PROPERTY STRINGS MODBUS USB_CDC)
message("Braille input source: " ${BRAILLE_INPUT})

if(BRAILLE_INPUT STREQUAL "USB_CDC")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE BRAILLE_INPUT_USB_CDC)
else()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE BRAILLE_INPUT_MODBUS)

    # Drop the USB application sources and the class-stack middleware. The
    # remaining references from generated code are satisfied by
    # App/Src/usb_device_stub.c.
    get_target_property(_braille_sources ${CMAKE_PROJECT_NAME} SOURCES)
    list(FILTER _braille_sources EXCLUDE REGEX "/USB_DEVICE/")
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY SOURCES ${_braille_sources})

    get_target_property(_braille_libs ${CMAKE_PROJECT_NAME} LINK_LIBRARIES)
    list(REMOVE_ITEM _braille_libs USB_Device_Library)
    set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY LINK_LIBRARIES ${_braille_libs})

    # The object library is still defined by the generated CMakeLists; keep
    # ninja from compiling objects nothing links.
    set_property(TARGET USB_Device_Library PROPERTY EXCLUDE_FROM_ALL TRUE)
endif()
```

- [ ] **Step 3: Verify the MODBUS variant links and measure the saving**

```bash
export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
rm -rf build/Debug build/Release
cmake --preset Debug -DBRAILLE_INPUT=MODBUS && cmake --build --preset Debug
cmake --preset Release -DBRAILLE_INPUT=MODBUS && cmake --build --preset Release
echo "--- MODBUS variant ---"
arm-none-eabi-size build/Debug/single-cell-braille-controller.elf
arm-none-eabi-size build/Release/single-cell-braille-controller.elf
grep -c "usbd_cdc\|usbd_core\|usbd_ctlreq" build/Release/single-cell-braille-controller.map || echo "USB class stack absent from the map"
```
Expected: both link, and the class-stack objects are gone from the map. Compare against the numbers recorded in Task 10 to get the realised saving.

- [ ] **Step 4: Verify the USB variant still builds today's firmware**

```bash
rm -rf build/Debug
cmake --preset Debug -DBRAILLE_INPUT=USB_CDC && cmake --build --preset Debug
arm-none-eabi-size build/Debug/single-cell-braille-controller.elf
```
Expected: links. `App/Src/app_main.c` still has its CDC body at this point, so this variant is the pre-existing firmware plus the unreferenced new modules. Then reconfigure back to MODBUS so later work uses the default:
```bash
rm -rf build/Debug && cmake --preset Debug && cmake --build --preset Debug
```

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt App/Src/usb_device_stub.c
git commit -m "build: select the input source at configure time

BRAILLE_INPUT=MODBUS (the default) or USB_CDC. The MODBUS variant drops the
USB application sources and the class-stack middleware from the link; the USB
variant rebuilds the typed-character firmware unchanged, which is worth
keeping for bench work on the braille rendering with no PLC in the room.

The switch lives in the top-level CMakeLists.txt, not in
cmake/stm32cubemx/CMakeLists.txt, because the latter is regenerated by CubeMX
and any edit there is temporary by construction.

Two references to USB survive in generated code and cannot be guarded:
main() calls MX_USB_DEVICE_Init(), and stm32f1xx_it.c's USB handler calls
HAL_PCD_IRQHandler(&hpcd_USB_FS) -- kept alive by the vector table even with
--gc-sections. Both are outside any USER CODE region, so a guard would be
overwritten on the next generation. usb_device_stub.c supplies no-op
definitions instead, which means the reachable part of the PCD/LL layer stays
linked. That cost is accepted: Release has 43 KB free and this change is about
not shipping a dead USB stack, not about making the feature fit.

Release text: <record both variants' arm-none-eabi-size output here>.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 12: Wire it together

Spec §4. The first task where the whole system runs. `App_run()` becomes the cooperative tick.

**Files:**
- Modify: `App/Inc/app_main.h`, `App/Src/app_main.c`, `Core/Src/main.c` (inside `USER CODE BEGIN 2` only)

**Interfaces:**
- Consumes: everything from Tasks 1–11.
- Produces: `void App_init(UART_HandleTypeDef *huart_m1, UART_HandleTypeDef *huart_m2, TIM_HandleTypeDef *htim_m1, TIM_HandleTypeDef *htim_m2, TIM_HandleTypeDef *htim_buzzer, SPI_HandleTypeDef *hspi_eth)` and the existing `void App_run(void)`.

- [ ] **Step 1: Update `app_main.h`**

Extend `App_init()` with the two new handles, keeping the file's existing convention of passing peripherals in explicitly rather than reaching for Core globals:

```c
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_spi.h"
#include "stm32f1xx_hal_tim.h"

/**
 * @brief One-time application setup.
 *
 * Brings up both motors and homes the discs, then -- in the MODBUS variant --
 * the buzzer, buttons, reader and PLC link. The last two parameters are
 * unused in the USB_CDC variant.
 *
 * @param huart_m1     UART bound to motor 1's TMC2209.
 * @param huart_m2     UART bound to motor 2's TMC2209.
 * @param htim_m1      STEP timer for motor 1.
 * @param htim_m2      STEP timer for motor 2.
 * @param htim_buzzer  PWM timer driving the buzzer.
 * @param hspi_eth     SPI peripheral wired to the W5500.
 */
void App_init(UART_HandleTypeDef *huart_m1, UART_HandleTypeDef *huart_m2,
              TIM_HandleTypeDef *htim_m1, TIM_HandleTypeDef *htim_m2,
              TIM_HandleTypeDef *htim_buzzer, SPI_HandleTypeDef *hspi_eth);

/**
 * @brief One pass of the cooperative main loop; called forever.
 *
 * In the MODBUS variant this services buttons, the PLC link and the
 * presentation sequencer, in that order, and returns. It blocks only while
 * the discs are turning or a buzzer pattern is playing.
 */
void App_run(void);
```

- [ ] **Step 2: Rewrite `app_main.c`**

```c
// ----------------------------------------------------------------------------
// app_main.c
//
// The wiring layer: it owns no behaviour, only the decision about which
// modules are connected to which.
//
// In the MODBUS variant App_run() is a cooperative tick. Buttons are serviced
// first because they are the only thing a person is waiting on; the link next,
// so a poll goes out as soon as it is due; the sequencer last, since its work
// is timer-driven and a pass of delay costs nothing.
//
// Two calls inside this loop block, both bounded and both deliberate: a disc
// move takes about a second, and a buzzer pattern up to 480 ms. Button presses
// are latched by their interrupt throughout, so none is lost.
// ----------------------------------------------------------------------------

#include "app_main.h"

#include "braille_disc.h"
#include "main.h"
#include "motion_planner.h"
#include "stm32f1xx_hal.h"

#if defined(BRAILLE_INPUT_USB_CDC)

#include "usbd_cdc_if.h"

void App_init(UART_HandleTypeDef *huart_m1, UART_HandleTypeDef *huart_m2,
              TIM_HandleTypeDef *htim_m1, TIM_HandleTypeDef *htim_m2,
              TIM_HandleTypeDef *htim_buzzer, SPI_HandleTypeDef *hspi_eth) {
  (void)htim_buzzer;
  (void)hspi_eth;
  initialize_motors(huart_m1, huart_m2, htim_m1, htim_m2);
  // A disc that fails to home already blinks its motor index; the app still
  // starts, so USB stays usable for diagnosing the sensor.
  (void)calibrate_zero_position();
}

void App_run(void) {
  uint8_t c; // One byte character buffer for user input

  if (!CDC_ReadChar(&c)) {
    return;
  }

  // Echo the byte straight back so the host terminal shows what arrived. This
  // proves both USB directions before any motion is involved.
  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
  CDC_Transmit_FS(&c, 1u);

  // Once the echo is confirmed, drive the disc from the received character:
  translate_char_on_disc(c);
}

#else // BRAILLE_INPUT_MODBUS

#include "buttons.h"
#include "buzzer.h"
#include "plc_link.h"
#include "reader_ui.h"

/** @brief Timer channel the buzzer is wired to (PB6 is TIM4_CH1). */
#define APP_BUZZER_TIM_CHANNEL TIM_CHANNEL_1

// Every signature already lines up, so these tables are the whole of the
// wiring: no adapter functions are needed between the reader, the renderer,
// the buzzer and the link.
static const reader_ui_io_t app_reader_io = {
    .render_char = braille_render_char,
    .render_dots = braille_render_dots,
    .play_sound = buzzer_play,
    .request_poll = plc_link_request_now,
    .now_ms = HAL_GetTick,
};

static const plc_link_io_t app_link_io = {
    .on_snapshot = reader_ui_on_snapshot,
    .on_fault = reader_ui_on_link_fault,
};

void App_init(UART_HandleTypeDef *huart_m1, UART_HandleTypeDef *huart_m2,
              TIM_HandleTypeDef *htim_m1, TIM_HandleTypeDef *htim_m2,
              TIM_HandleTypeDef *htim_buzzer, SPI_HandleTypeDef *hspi_eth) {
  initialize_motors(huart_m1, huart_m2, htim_m1, htim_m2);
  // A disc that fails to home blinks its motor index and startup continues,
  // so a sensor fault does not cost the operator the PLC readout as well.
  (void)calibrate_zero_position();

  // The buzzer comes up before the link, so a W5500 that is missing can say
  // so out loud from inside plc_link_init().
  buzzer_init(htim_buzzer, APP_BUZZER_TIM_CHANNEL);
  buttons_init();
  reader_ui_init(&app_reader_io);
  plc_link_init(hspi_eth, &app_link_io);
}

void App_run(void) {
  reader_event_t event = buttons_take_event();
  if (event != READER_EV_NONE) {
    reader_ui_on_event(event);
  }

  // May call straight back into the reader with a snapshot or a fault, which
  // can render and therefore block. That is fine here: it is the same thread,
  // and a package arriving is exactly when the discs should move.
  plc_link_tick();

  reader_ui_tick();
}

/**
 * @brief EXTI callback for every line on this board.
 *
 * PB5 (the W5500 interrupt) and PB7/PB8/PB9 (the buttons) share
 * EXTI9_5_IRQn, so the two owners are simply offered every pin and each
 * ignores what is not theirs. Both do nothing but set a flag.
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  buttons_on_exti(GPIO_Pin);
  plc_link_on_exti(GPIO_Pin);
}

#endif // BRAILLE_INPUT_USB_CDC
```

- [ ] **Step 3: Update the call in `Core/Src/main.c`**

Inside `/* USER CODE BEGIN 2 */` only — this is a legitimate edit, it is a user region:

```c
  App_init(&huart2, &huart3, &htim2, &htim3, &htim4, &hspi2);
```

`main.c` already includes `tim.h` and `spi.h` via the generated includes, so `htim4` and `hspi2` are in scope. Confirm with a build rather than by reading.

- [ ] **Step 4: Build both variants**

```bash
export PATH="$HOME/.local/share/stm32cube/bundles/cmake/4.3.1+st.1/bin:$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/13.3.1+st.9/bin:$HOME/.local/share/stm32cube/bundles/ninja/1.13.2+st.1/bin:$PATH"
rm -rf build/Debug build/Release
cmake --preset Debug && cmake --build --preset Debug
cmake --preset Release && cmake --build --preset Release
arm-none-eabi-size build/Debug/single-cell-braille-controller.elf
arm-none-eabi-size build/Release/single-cell-braille-controller.elf
make -C tests
rm -rf build/Debug
cmake --preset Debug -DBRAILLE_INPUT=USB_CDC && cmake --build --preset Debug
rm -rf build/Debug && cmake --preset Debug && cmake --build --preset Debug
```
Expected: all four builds link, host suites pass.

- [ ] **Step 5: End-to-end verification on hardware**

Set up: the PLC (or the ScanBus simulator acting as a server) reachable at `10.0.0.204:503` serving 11 coils from address 0, and the device on the same subnet at `10.0.0.50`. Flash the Release MODBUS build.

Work through this list and record the result of each:

1. **Boot** — discs home as before, then the confirm tone within a couple of seconds once the first poll lands. Cell stays blank.
2. **No PLC** — unplug the cable and power-cycle: the low double fault tone, repeating no more often than every 10 s. Buttons still respond. Plug the cable back in: it recovers on its own and confirms.
3. **No W5500** — pull the module: three LED blinks at boot plus the fault tone, and the discs and buttons still work.
4. **First NEXT** — `A`, then `E` about 1.5 s later, then the value glyph, which stays. Cross-check the value against ScanBus's `SMEMA A IN` bit.
5. **Walk the package** — eleven presses give `AE AS BE BS P S T V R M L`, each ending in a flat or raised cell. Check each value against ScanBus.
6. **REPEAT** — replays the current field from its first glyph.
7. **PREV** — steps back one field; at field 0 it blips and the discs do not move.
8. **NEXT at the last field, nothing changed** — rising pair, then the confirm tone, then back to `A`.
9. **NEXT at the last field with something changed** — toggle a coil in ScanBus mid-read: the triple chirp fires. Then NEXT at the last field jumps straight to `A` of the new package with a confirm tone and no request tone.
10. **Value correctness** — set a coil true in ScanBus and confirm that field reads all-six-raised, and all-flat when false. Verify with a finger, not by eye.
11. **Impatient tapping** — tap NEXT three times fast during a disc move: the reader advances one field, not three.
12. **Cable pulled mid-read** — fault tone; the package in hand is still readable with REPEAT and PREV.

Anything that fails here is a real bug, not a tuning issue — take it back to the module that owns it and add the case to that module's host suite before fixing it.

- [ ] **Step 6: Commit**

```bash
git add App/Inc/app_main.h App/Src/app_main.c Core/Src/main.c
git commit -m "feat: drive the braille cell from the PLC over MODBUS TCP

App_run() becomes a cooperative tick: buttons first, because they are the only
thing a person is waiting on; the link next, so a poll goes out as soon as it
is due; the sequencer last, since its work is timer-driven. Only two calls in
the loop block, both bounded -- a disc move of about a second and a buzzer
pattern of up to 480 ms -- and presses stay latched by their interrupt
throughout.

The wiring turned out to be two initialiser tables and nothing else. Every
signature already lines up, so no adapter functions sit between the reader,
the renderer, the buzzer and the link -- which is the payoff for defining the
reader's IO as function pointers rather than letting it call the modules
directly.

Both EXTI owners are offered every pin and each ignores what is not theirs,
because PB5 and the three buttons share EXTI9_5_IRQn.

The USB typed-character path is preserved verbatim under
BRAILLE_INPUT_USB_CDC, including its LED toggle and echo.

Verified end to end against the PLC: boot, the full eleven-field walk with
values cross-checked in ScanBus, prev/repeat/boundary behaviour, an
out-of-cycle request at the end of a package, the queued-package chirp and
automatic jump to newer data, recovery from an unplugged cable and from a
missing W5500, and press coalescing during a move.

Release text: <record arm-none-eabi-size output here>.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 13: Documentation

Spec §13. The `docs/` guides are the project's explanation of itself, and four of them now describe a firmware that no longer exists by default.

**Files:**
- Create: `docs/09-modbus-tcp-and-plc-link.md`, `docs/10-reader-ui-and-buzzer.md`
- Modify: `docs/README.md`, `docs/01-architecture-layers.md`, `docs/02-execution-flow.md`, `docs/06-usb-cdc.md`

- [ ] **Step 1: Write `docs/09-modbus-tcp-and-plc-link.md`**

Match the existing guides' voice: explain what the code does and why it has to work that way, not what changed. Cover, with line-anchored links into the real files:

- Why the device is a client and what that means for "unrequested" data — the single most confusing thing about this design for anyone who read the original brief.
- A MODBUS TCP frame, byte by byte, for the actual 12-byte request and 11-byte response this firmware sends.
- Why the parser is strict, with the stale-transaction-id failure spelled out: a reply to an earlier request decodes perfectly and would show wrong values silently.
- The `plc_link` state diagram and what each transition is waiting for.
- The vendoring decision: `Ethernet/` only, and why `Internet/` would be dead code.
- `ETH_INT` as an optimisation the design does not depend on.
- A troubleshooting section reusing the `VERSIONR` ladder from Task 9 Step 7.

- [ ] **Step 2: Write `docs/10-reader-ui-and-buzzer.md`**

- The reading model: why one press plays a whole item and rests on the value.
- The label table and the all-flat/all-raised invariant that makes mixed-length labels readable — including that `É` is all six dots and must never become a label.
- The four states and the full event table.
- One pending slot rather than a queue, with the arithmetic: 11 fields at roughly 45 s against a poll every 500 ms.
- Why the buzzer blocks, and the five patterns with their frequencies and durations.
- Why `reader_ui` takes an IO struct, and how `tests/test_reader_ui.c` uses it — a worked example of the command-log encoding, since that is the file a future maintainer will need to read.

- [ ] **Step 3: Update the four existing guides**

- `docs/README.md` — add both guides to the numbered index with one-sentence descriptions; extend the hardware pin map with `ETH_INT` PB5, `BUZZER` PB6/TIM4_CH1, `BTN_REPEAT` PB7, `BTN_NEXT` PB8, `BTN_PREV` PB9, `ETH_NSS` PB12, SPI2 PB13–PB15, `ETH_RESET` PA10; note that the intro's "character typed into a USB virtual COM port" is now the non-default variant.
- `docs/01-architecture-layers.md` — add the six new App modules, `Lib/w5500`, and `fault_led` as the shared blink-code owner; the App layer is now nine modules, not five.
- `docs/02-execution-flow.md` — replace the CDC-driven loop trace with the cooperative tick, and add W5500 bring-up and the first poll to the power-on sequence.
- `docs/06-usb-cdc.md` — open with a note that this path is now `-DBRAILLE_INPUT=USB_CDC` and not built by default, why it was kept, and what the MODBUS variant stubs out and why it cannot simply be guarded.

- [ ] **Step 4: Verify every link and anchor resolves**

Line-anchored links have silently rotted in this repo before, after a comment reflow shifted `docs/07`'s anchors by three lines. Check them:

```bash
grep -ohE '\]\([0-9a-zA-Z._/-]+\.md#L[0-9]+(-L[0-9]+)?\)' docs/*.md | sort -u
grep -ohE '\]\((\.\./)*[0-9a-zA-Z._/-]+\.(md|c|h)\)' docs/*.md | sort -u
```
Open each target and confirm the cited line still says what the prose claims. Then regenerate Doxygen and confirm no new warnings — an unbalanced `@name`/`@{` has broken this build before:
```bash
doxygen Doxyfile 2>&1 | grep -i "warning" || echo "no Doxygen warnings"
```

- [ ] **Step 5: Commit**

```bash
git add docs/
git commit -m "docs: document the MODBUS TCP reader

Two new guides and four updates. The guides explain what the code does and
why it has to work that way, matching the voice of the existing eight rather
than reading as a changelog.

docs/09 leads with the thing most likely to confuse someone who read the
original requirement: the device is a MODBUS client, a server cannot push to
a client, and 'the PLC sent something unrequested' is really 'a background
poll came back different'. It also spells out why the parser is strict, since
a reply to an earlier request decodes perfectly and would show wrong values
with nothing to indicate a problem.

docs/10 covers the reading model and the invariant the label scheme rests on
-- no label glyph may be all-flat or all-raised, and E-acute is all six dots,
so it must never become a label. It works through the command-log encoding in
tests/test_reader_ui.c, which is the file a maintainer will need to read
before changing any of this behaviour.

docs/06 now opens by saying the USB path is the non-default variant, why it
was kept, and why the two generated references to it have to be stubbed rather
than guarded.

Line anchors and Doxygen re-verified; anchors in this repo have rotted before
when a comment reflow shifted them.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Plan self-review

Run against the spec after writing, before execution.

**Spec coverage.** Every section maps to a task: §4.2 braille change → Task 1; §4.2 fault_led → Task 2; §9 config → Task 3; §5 packet → Task 4; §8 codec → Task 5; §6 reader → Task 6; §7 sounds → Task 7; §6 buttons → Task 8; §4.1 `Lib/w5500` → Task 9; §8 link + §10 errors → Task 10; §3.6 variants → Task 11; §4 tick → Task 12; §13 docs → Task 13. §11 flash budget is covered by a measure-and-record step in every firmware task. §12 testing is Tasks 1, 4, 5 and 6. §14 out-of-scope adds nothing.

**Three spec corrections are folded into the tasks**, each with a step that edits the spec so the two do not drift:
1. Task 6 — a link fault returns to `RESTING`, not to "the previous state", which would resume a stale dwell.
2. Task 7 — the pattern bound is 500 ms, not 400 ms; `LINK_FAULT` is 480 ms.
3. Already committed in `3f3399c` — the flash budget was a Debug-build artefact.

**Type consistency.** `reader_sound_t` is defined once in `reader_ui.h` and consumed by `buzzer.h`; `reader_event_t` likewise by `buttons.h`. `plc_snapshot_t` is produced by `plc_packet.h` and flows through `plc_link_io_t.on_snapshot` into `reader_ui_on_snapshot` — same type, no conversion. `braille_render_char`/`braille_render_dots`/`buzzer_play`/`plc_link_request_now`/`HAL_GetTick` match `reader_ui_io_t`'s pointer types exactly, which Task 12 relies on to need no adapters. `PLC_FIELD_COUNT` and `PLC_COIL_COUNT` are tied by a static assertion rather than by convention.

**Known risks, in the order they will bite.**
1. **ioLibrary's API differs from the version documented** (Task 9) — `reg_wizchip_spiburst_cbfunc` may be absent or take `datasize_t`, and `wizphy_getphylink()` may sit behind a chip guard. Both have named fallbacks in the task; record whatever you find in `UPSTREAM.md`.
2. **SPI at 12 Mbit/s over jumper leads** (Task 9 Step 7) — the `VERSIONR` check catches it immediately, and the fix is one prescaler step.
3. **Debug flash** (Task 10 Step 3) — ioLibrary at `-O0` roughly doubles. If Debug nears 64 KB, pull Task 11 forward; Release is never at risk.
4. **`PLC_LABEL_DWELL_MS`** — 1500 ms is a guess about a person, not a number derivable from the code. Expect to tune it after Task 12 Step 5 with the actual reader.
