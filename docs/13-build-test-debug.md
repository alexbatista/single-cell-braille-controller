# 13. Build, test and debug

This document covers the development workflow: toolchain, building both
variants, flashing and debugging over SWD, the host unit tests, the generated
API reference, the PLC simulator, the bring-up sequence for a board, the LED
blink codes, and a troubleshooting table. The root [README](../README.md) has
a short version of the build and flash steps.

**Source files:** `CMakeLists.txt`, `CMakePresets.json`,
`cmake/gcc-arm-none-eabi.cmake`, `tests/Makefile`, `tests/test_support.h`,
`tests/stubs/`, `Doxyfile`, `.vscode/launch.json`, `tools/plc_sim.py`,
`tools/verify_plc_sim.py`, `App/Inc/fault_led.h`.

## 13.1 Toolchain

| Tool | Version used | Notes |
|---|---|---|
| `arm-none-eabi-gcc` | 13.3 or 14.3 (ST "gnu-tools-for-stm32") | any recent Arm GNU Toolchain with newlib-nano works |
| CMake | ≥ 3.22 | presets file version 3 |
| Ninja | any recent | generator used by the presets |
| STM32CubeProgrammer or OpenOCD | — | flashing over SWD |
| ST-Link V2 or compatible | — | SWD probe |
| host `gcc`, `make` | — | unit tests |
| Python 3 (standard library only) | — | simulator, wiring diagram |
| Doxygen (+ Graphviz `dot`) | — | optional, API reference |
| STM32CubeMX | — | only to change the `.ioc` |

`cmake/gcc-arm-none-eabi.cmake` finds the compiler in this order:

1. `$CUBE_BUNDLE_PATH/gnu-tools-for-stm32/13.3.1+st.9/bin` if the environment
   variable is set (the STM32 VS Code extension installs its tools as
   "bundles");
2. the newest xPack toolchain under `~/Library/xPacks`, `~/.local/xPacks` or
   `~/xPacks`;
3. `arm-none-eabi-gcc` on `PATH`.

When the tools come only from the STM32Cube bundles and are not on `PATH`,
export them before building, for example:

```bash
B=~/.local/share/stm32cube/bundles
export PATH=$B/cmake/<ver>/bin:$B/ninja/<ver>/bin:$B/gnu-tools-for-stm32/<ver>/bin:$PATH
```

## 13.2 Building

```bash
cmake --preset Debug                 # configure: build/Debug, MODBUS variant
cmake --build --preset Debug         # → build/Debug/single-cell-braille-controller.elf
```

| Preset | Flags | Output directory |
|---|---|---|
| `Debug` | `-O0 -g3` | `build/Debug/` |
| `Release` | `-Os -g0` | `build/Release/` |

Both use `-Wall -ffunction-sections -fdata-sections -fstack-usage`, link with
`--specs=nano.specs --gc-sections`, write a `.map` file and print flash/RAM
usage at the end of the link.

Variant selection (cached; reconfigure to switch back):

```bash
cmake --preset Debug -DBRAILLE_INPUT=USB_CDC
cmake --build --preset Debug
cmake --preset Debug -DBRAILLE_INPUT=MODBUS
```

`compile_commands.json` is exported into the build directory for clangd.
Before committing a change that touches firmware code, build all four
combinations (Debug/Release × MODBUS/USB_CDC); the variants compile different
code paths of `app_main.c`.

## 13.3 Flashing and debugging

Connect the ST-Link to the SWD header (3V3, SWDIO, SWCLK, GND).

```bash
# STM32CubeProgrammer
STM32_Programmer_CLI -c port=SWD -w build/Debug/single-cell-braille-controller.elf -v -rst

# OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c "program build/Debug/single-cell-braille-controller.elf verify reset exit"
```

`.vscode/launch.json` provides two debug configurations: *Debug with ST-Link*
(Cortex-Debug extension) and *STM32Cube: Launch ST-Link GDB Server* (STM32 VS
Code extension, builds first). The Cortex-Debug configuration's `executable`
is `./bin/executable.elf`; point it at
`build/Debug/single-cell-braille-controller.elf` before using it.

### Inspecting a running device without resetting it

A fault that disappears on reset can be read out live. Find a variable's
address in the ELF that is running, then read it with a hot-plug connection:

```bash
arm-none-eabi-nm build/Release/single-cell-braille-controller.elf | grep ' link$'
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -r32 <address> 4
```

`link` is the `plc_link.c` state struct; its first fields are the `io`
pointer, the SPI handle and `state`, whose value maps to `plc_link_state_t`
(0 = `PLC_LINK_CHIP_FAULT` … 5 = `PLC_LINK_BACKOFF`). The same method works
for `reader` in `reader_ui.c`. Temporarily adding diagnostic fields to such a
struct (a raw register value, which call failed, what it returned) is often the
fastest way to locate a fault in the field. Always take the address from the
ELF currently flashed; it changes with every build.

## 13.4 Host tests

The pure-logic modules are tested on the development machine with the system
`gcc`; no board or ARM toolchain is needed.

```bash
make -C tests          # build and run every suite; non-zero exit on any failure
make -C tests clean
```

| Suite | Module under test |
|---|---|
| `test_braille_dots` | `braille_disc.c`: value glyphs, every label letter's disc angles, case folding, one ASCII and one two-byte UTF-8 input |
| `test_plc_packet` | `plc_packet.c`: field table, label invariant, snapshot accessors |
| `test_modbus_tcp` | `modbus_tcp.c`: request bytes, every parser rejection path |
| `test_reader_ui` | `reader_ui.c`: every row of the event table, through recorded output sequences |
| `test_buttons` | `buttons.c`: press and release debounce |

How the suites work:

- Each suite is one executable that `#include`s the module's `.c` file
  directly, so it can reach `static` functions and state. Compiled with
  `-std=c11 -Wall -Wextra -Werror`.
- `tests/stubs/main.h` and `tests/stubs/stm32f1xx_hal.h` replace the HAL with
  the few types and functions the modules touch; fakes (clock, pin levels,
  render and sound recorders) live in each suite.
- `tests/test_support.h` provides `CHECK`, `CHECK_EQ`, `CHECK_STR_EQ` and
  `TESTS_REPORT`. Each suite prints one summary line and exits non-zero on
  failure.
- The Makefile rebuilds every suite when any file in `App/` changes.

Adding a suite: create `tests/test_<name>.c` following an existing suite,
include the module's `.c`, add `test_<name>` to `SUITES` in `tests/Makefile`.

Not covered by host tests: `stepper.c`, `stepper_hal.c`, `motion_planner.c`
(homing included), `plc_link.c`, `buzzer.c`, the ports in `Lib/`. These are
verified on hardware.

## 13.5 API reference

```bash
doxygen Doxyfile       # → docs/html/index.html (not tracked by git)
```

The input is `App/` and the root `README.md`; `Lib/` and `Core/` are excluded.
`BRAILLE_INPUT_MODBUS` is predefined, so the reference documents the default
variant. The call graphs (`CALL_GRAPH = YES`) need Graphviz; caller graphs are
off.

## 13.6 PLC simulator

`tools/plc_sim.py` is a minimal Modbus TCP server that stands in for the PLC,
standard library only. Check the simulator itself first, then run it:

```bash
python3 tools/verify_plc_sim.py              # self-test on loopback: no network, no board

sudo ip addr add 10.0.0.204/24 dev <iface>   # give the bench PC the PLC's address
sudo python3 tools/plc_sim.py --toggle       # serve 11 coils; flip one every 60 s
```

`verify_plc_sim.py` starts the simulator on a loopback port, sends frames built
exactly as `modbus_tcp.c` builds them, and checks the replies in the same order
as `modbus_parse_read_coils()`. Run it before trusting the simulator: the
firmware's parser rejects any malformed reply, so a simulator bug looks like a
firmware bug.

| Option | Default | Meaning |
|---|---|---|
| `--bind` | 10.0.0.204 | listen address (must exist on an interface) |
| `--port` | 503 | TCP port (privileged, hence `sudo`) |
| `--unit` | 1 | unit id answered |
| `--count` | 11 | coils served |
| `--coils` | all 0 | initial states, leftmost = coil 0, e.g. `00001000000` |
| `--toggle [SECONDS]` | off; bare = 60 s | flip one coil in rotation, to exercise the "newer data" path |
| `--quiet` | off | log only connections and toggles |

The defaults match `plc_config.h`; `--bind` and `--port` are for benches that
cannot use the real addressing. Remove the extra address afterwards with
`sudo ip addr del 10.0.0.204/24 dev <iface>`.

## 13.7 Blink codes

Reported once at start-up by `fault_led_blink()`: *n* short blinks, a pause,
repeated three times. Start-up then continues.

| Blinks | Constant | Meaning |
|---|---|---|
| 1 | `FAULT_LED_CODE_MOTOR_01` | motor 01: TMC2209 not answering over UART, or homing failed |
| 2 | `FAULT_LED_CODE_MOTOR_02` | motor 02: same |
| 3 | `FAULT_LED_CODE_ETHERNET` | W5500 absent or not answering over SPI (`VERSIONR` ≠ 0x04) |

A motor can blink twice during one start-up: once from the driver check and
once from homing. The LED stays off in normal operation (active-high LED, see
[02-mcu-resources.md](02-mcu-resources.md#22-pin-map)).

## 13.8 Bring-up sequence

For a new or repaired board, verify one subsystem at a time; each step assumes
the previous one passed.

1. **Firmware running.** After flashing, both discs turn within the first
   seconds (homing). If nothing moves and no LED code appears, the MCU is not
   running this firmware: reflash.
2. **Drivers and homing.** No blink code 1 or 2. Both discs stop at their tab.
   Otherwise see [04-tmc2209.md](04-tmc2209.md#44-failure-behaviour) and
   [05-motion-and-homing.md](05-motion-and-homing.md#58-when-homing-fails).
3. **W5500 over SPI.** No blink code 3. Otherwise see
   [07-w5500.md](07-w5500.md#78-troubleshooting-the-chip).
4. **Buzzer.** With the cable connected and no PLC present, the link-fault
   sound (low double tone) plays about every 10 s.
5. **On the network.** From a Linux host on the same switch:

   ```bash
   sudo ip addr add 10.0.0.9/24 dev <iface>   # put the host on the device's subnet
   sudo tcpdump -i <iface> -n arp             # watch for at least 15 s
   ```

   With no PLC present the device repeatedly asks
   `who-has 10.0.0.204 tell 10.0.0.50`: that single line proves SPI, the
   configured address, PHY link and the link state machine. Then
   `ping 10.0.0.50`; `arping` shows the MAC `00:08:dc:11:22:33`. A port scan
   finds nothing open, which is correct: the device is a client and never
   listens.
6. **Buttons.** Before any data: PREV and REPEAT give the short low blip;
   NEXT gives the rising pair, then (with no PLC) the fault tone after
   `PLC_RESPONSE_TIMEOUT_MS`, unless a fault tone already played in the last
   10 s (`PLC_FAULT_SOUND_MIN_INTERVAL_MS`).
7. **End to end with the simulator** (`plc_sim.py --toggle`): about half a
   second after the link connects, the first background poll arrives and the
   confirm tone plays with the cell still blank. NEXT then shows `A`, `E`,
   value. (NEXT pressed before that first poll returns plays the rising pair
   first.) Walk all 11 fields; a toggle during
   the read gives the triple chirp once; NEXT past the last field adopts the
   newer package.
8. **End to end with the PLC.** Same as step 7, with values checked against the
   PLC's own HMI.

## 13.9 Troubleshooting

| Symptom | Cause | Check |
|---|---|---|
| Nothing moves at power-up, no LED code | firmware not running, or stuck in `Error_Handler` / a fault handler | reflash; attach the debugger and halt to see where it is |
| Blink 1 or 2, disc turns | TMC2209 UART not answering | PDN wiring, 1 kΩ on TX, VCC_IO ([04-tmc2209.md](04-tmc2209.md)) |
| Blink 1 or 2, disc does not stop | homing failed | sensor power, tab in the slot ([05-motion-and-homing.md](05-motion-and-homing.md)) |
| Wrong dots, consistently | DIR inversion wrong for the mechanism | `APP_MOTOR_DIR_INVERTED` ([05-motion-and-homing.md](05-motion-and-homing.md#54-which-way-is-forward--app_motor_dir_inverted)) |
| Wrong dots, varying from boot to boot | disc not homed | blink codes at start-up |
| Blink 3, RJ45 LEDs lit | SPI wiring (MISO/MOSI swap most often) | [07-w5500.md](07-w5500.md#78-troubleshooting-the-chip) |
| No ARP from the device, no sound, no blink | link never started | reflash; confirm the MODBUS variant was built |
| Fault tone every ~10 s, ARP `who-has <PLC>` | PLC unreachable | PLC address, subnet, cable to the same switch |
| Fault tone on every poll while connected | responses rejected | unit id, coil range, port ([08-modbus-tcp.md](08-modbus-tcp.md#87-diagnosing-the-link)) |
| One press read as two | contact bounce beyond 40 ms | raise `PLC_BUTTON_RELEASE_STABLE_MS` |
| Buzzer clicks instead of beeping | active buzzer fitted, or transistor stage miswired | passive piezo, BC368 wiring ([10-sound-feedback.md](10-sound-feedback.md)) |
