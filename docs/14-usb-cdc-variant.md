# 14. USB CDC bench variant

The USB_CDC build replaces the PLC reader with a direct input path: characters
typed into a USB virtual COM port on a PC are rendered on the cell one by one.
It has no buttons, buzzer, reader or network. It exists to test the braille
rendering and the mechanics without a PLC. This document covers how to build
it, how the USB stack is organised, the receive path and the UTF-8 decoder.

**Source files:** `App/Src/app_main.c` (USB_CDC branch),
`USB_DEVICE/App/usbd_cdc_if.c`, `USB_DEVICE/App/usb_device.c`,
`USB_DEVICE/Target/usbd_conf.c`, `Middlewares/ST/STM32_USB_Device_Library/`,
`App/Src/braille_disc.c` (`translate_char_on_disc()`, `utf8_decode_byte()`),
`CMakeLists.txt`.

## 14.1 Building and using it

```bash
cmake --preset Debug -DBRAILLE_INPUT=USB_CDC
cmake --build --preset Debug
# flash as usual, then connect the Blue Pill's USB port to the PC
```

The device enumerates as a CDC ACM serial port (`/dev/ttyACM*` on Linux, a COM
port on Windows). Open it with any terminal program; the baud rate setting is
ignored. Each byte received is echoed back, the LED toggles, and the character
is rendered.

**Power:** in this variant the Blue Pill is powered from USB, and its own
regulator drives its 3V3 pin. The motors still need the 12 V supply, so before
plugging in USB **disconnect the Blue Pill's 3V3 pin from the power
breakout's 3.3 V rail** (the rail keeps feeding the TMC2209 VCC_IO and the
slot sensors) and keep GND common. Never connect USB while the 3V3 pin is
still tied to the breakout: two regulators would drive the same rail (see the
caution in the root README).

## 14.2 What runs

`App_init()` in this branch only calls `initialize_motors()` and
`calibrate_zero_position()`; the buzzer timer and SPI handles are ignored. A
homing failure still blinks its code and start-up continues, so the USB port
comes up for diagnosis.

`App_run()`:

1. `CDC_ReadChar(&c)` pops one byte from the receive ring buffer; if empty,
   return.
2. Toggle the LED and echo the byte with `CDC_Transmit_FS(&c, 1)`.
3. `translate_char_on_disc(c)` decodes and renders (blocks while the discs
   move).

`MX_USB_DEVICE_Init()` (generated) starts the USB device stack; the USB
interrupt `USB_LP_CAN1_RX0_IRQn` is enabled at priority 0.

## 14.3 Code layout

| Directory | Content | Owner |
|---|---|---|
| `Middlewares/ST/STM32_USB_Device_Library/Core` | USB device core: enumeration, control requests, endpoint dispatch | ST, never edited |
| `Middlewares/ST/STM32_USB_Device_Library/Class/CDC` | CDC ACM class | ST, never edited |
| `USB_DEVICE/Target/usbd_conf.c` | glue between the core and the HAL PCD driver; PMA layout; USB interrupt setup | CubeMX |
| `USB_DEVICE/App/usbd_desc.c` | device descriptors | CubeMX |
| `USB_DEVICE/App/usbd_cdc_if.c` | the application's CDC callbacks, receive ring buffer, `CDC_ReadChar()` | CubeMX file, project code in USER CODE blocks |

In the MODBUS build none of this is compiled; see
[01-architecture.md](01-architecture.md#15-build-variants).

## 14.4 Receive path

```
host sends an OUT packet on the CDC data endpoint
  → USB_LP_CAN1_RX0_IRQHandler()            (stm32f1xx_it.c)
  → HAL_PCD_IRQHandler(&hpcd_USB_FS)        reads the packet from PMA
  → HAL_PCD_DataOutStageCallback()          (usbd_conf.c)
  → USBD_LL_DataOutStage() → USBD_CDC_DataOut()
  → CDC_Receive_FS(Buf, Len)                (usbd_cdc_if.c) — still in the ISR
       copies bytes into cdc_rx_ring[] (64 B); drops the rest if full;
       re-arms reception with USBD_CDC_ReceivePacket()
main loop, later:
  App_run() → CDC_ReadChar(&c)              pops one byte
```

`CDC_Receive_FS()` runs in interrupt context: it only copies bytes. The ring
buffer has a single producer (the ISR, writes `cdc_rx_head`) and a single
consumer (the main loop, writes `cdc_rx_tail`), so it needs no locking.
Bytes arriving while the ring is full are lost; typing faster than the discs
can move overflows it after 63 pending bytes.

### Transmit path

`CDC_Transmit_FS()` queues the bytes into the USB packet memory and returns at
once; the packet goes out when the host polls the IN endpoint. If the previous
packet has not been collected yet it returns `USBD_BUSY` and sends nothing.
`App_run()` ignores the return value, so under fast typing some **echoes** may
be missing; received characters are not affected.

## 14.5 UTF-8 decoder

Terminals send UTF-8, so an accented letter arrives as two bytes (`ç` is
`0xC3 0xA7`). Rendering byte by byte would show `Ã` (Latin-1 `0xC3`) and then a
blank. `translate_char_on_disc()` feeds each byte to `utf8_decode_byte()` and
renders only completed codepoints.

Decoder state: `utf8_codepoint` (bits collected), `utf8_min_codepoint`
(smallest value legal for the sequence length), `utf8_bytes_remaining`.

| Byte | Pattern | Action |
|---|---|---|
| `0x00–0x7F` | `0xxxxxxx` | ASCII: return it; abandon any sequence in progress |
| `0x80–0xBF` | `10xxxxxx` | continuation: if no sequence is open, drop it; else shift in 6 bits; when the last one arrives, validate and return the codepoint |
| `0xC0–0xDF` | `110xxxxx` | lead of 2 bytes, minimum U+0080 |
| `0xE0–0xEF` | `1110xxxx` | lead of 3 bytes, minimum U+0800 |
| `0xF0–0xF7` | `11110xxx` | lead of 4 bytes, minimum U+10000 |
| `0xF8–0xFF` | — | never valid: reset |

A lead byte always starts a new sequence, so malformed input can never leave
the decoder stuck. A completed sequence is rejected (nothing rendered) if it is:

- **overlong** — below the minimum for its length (`0xC1 0x81` would otherwise
  decode to `A`);
- a **surrogate half**, U+D800–U+DFFF;
- **above U+10FFFF**.

`UTF8_INCOMPLETE` (`0xFFFFFFFF`) is returned while a sequence is open and for
rejected sequences; the discs do not move. A valid codepoint ≤ U+00FF is
rendered through the Latin-1 glyph table
([06-braille-encoding.md](06-braille-encoding.md#63-glyph-table)); anything
above renders as a blank cell rather than aliasing to another letter.

`tests/test_braille_dots.c` covers only an ASCII byte and one two-byte
sequence (`ç`). Stray continuation bytes, three- and four-byte sequences and
the three rejection rules have no host test.
