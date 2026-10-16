# 11. Adding a protocol

This document explains where a second industrial protocol plugs into the
firmware, which parts are reusable unchanged, which part currently fuses
transport and protocol, and the contract any new link must honour so that the
reader, buttons and sounds keep behaving exactly as they do with Modbus TCP.
No second protocol exists yet; the interfaces below marked *proposal* are not
implemented.

**Source files:** `App/Src/app_main.c`, `App/Inc/plc_link.h`,
`App/Src/plc_link.c`, `App/Inc/reader_ui.h`, `App/Inc/plc_packet.h`,
`App/Src/modbus_tcp.c`, `CMakeLists.txt`.

## 11.1 The seam

The reader does not know which protocol is behind it. It depends on exactly
two things:

1. **Inbound:** a `plc_snapshot_t` (up to 16 boolean fields) delivered to
   `reader_ui_on_snapshot()`, or a call to `reader_ui_on_link_fault()`.
2. **Outbound:** the `request_poll` entry of `reader_ui_io_t`, called when the
   operator needs fresh data now.

`app_main.c` makes the connection with two tables:

```c
static const reader_ui_io_t app_reader_io = {
    .render_char = braille_render_char,
    .render_dots = braille_render_dots,
    .play_sound = buzzer_play,
    .request_poll = plc_link_request_now,   /* ← link entry point */
    .now_ms = HAL_GetTick,
};

static const plc_link_io_t app_link_io = {
    .on_snapshot = reader_ui_on_snapshot,   /* ← link reports here */
    .on_fault = reader_ui_on_link_fault,
};
```

A new protocol is a module that provides the same four entry points as
`plc_link` and calls the same two callbacks.

## 11.2 What is reusable

| Module | Protocol-specific? | Reused by a new protocol |
|---|---|---|
| `reader_ui` | no | unchanged |
| `plc_packet` (field table, snapshot) | no; "coil" is just a bit index | unchanged, or with a new field table |
| `buttons`, `buzzer`, `fault_led` | no | unchanged |
| `braille_disc`, `motion_planner`, `stepper` | no | unchanged |
| `modbus_tcp` | Modbus framing only, no sockets | reused by any Modbus transport (e.g. RTU framing would need a new codec, the PDU layout is the same) |
| `Lib/w5500` + `w5500_stm32` | Ethernet transport | reused by any TCP or UDP protocol |
| `plc_link` | **yes**: fuses W5500 bring-up, the TCP connection, Modbus polling policy and `plc_config.h` addressing | replaced or split (section 11.4) |

## 11.3 Contract for a link module

| Entry point | Called from | Must | Must not |
|---|---|---|---|
| `init(…, const plc_link_io_t *io)` | `App_init()`, once, after `reader_ui_init()` | bring the transport up or enter a retry state; on a missing device, blink a fault code and report one fault | halt start-up; block for more than tens of milliseconds |
| `tick(void)` | `App_run()`, every pass | advance its own state machine; deliver at most one snapshot or fault per call | block (the reader's dwell and buttons share the loop) |
| `request_now(void)` | the reader, through `request_poll` | schedule a read as soon as possible | start a second request while one is in flight (ignore the call instead) |
| `on_exti(uint16_t pin)` | `HAL_GPIO_EXTI_Callback()`, interrupt context — only if it owns an EXTI pin | set a flag and return | do anything else in interrupt context |

Delivery rules:

- **Snapshots are whole packages.** Call `on_snapshot()` only with a fully
  validated package covering all `PLC_FIELD_COUNT` fields. Never deliver a
  partial or unvalidated read; report a fault instead.
- **Faults are reported, not rate-limited.** Call `on_fault()` for every
  failure; the reader limits how often the operator hears it. Do report
  periodically while the link is down (e.g. once per reconnect attempt), so
  silence is never mistaken for health.
- **Keep polling in the background.** The reader's "newer data is waiting"
  behaviour depends on snapshots arriving regularly, not only when requested.
- **Answer requests within `PLC_RESPONSE_TIMEOUT_MS`**, or the reader times out
  its own wait and reports a fault.
- **More than 16 fields** requires widening `plc_snapshot_t` (and the
  `_Static_assert` in `plc_packet.c`); the reader indexes fields through
  `plc_snapshot_bit()` and is otherwise unaffected.

## 11.4 Two cases

### Another protocol over the W5500

`plc_link.c` would be split into a transport layer (chip bring-up, PHY wait,
connect, backoff, send/receive) and a protocol policy (what to send, how to
validate, when to poll). A *proposal* for the split:

```c
/* eth_link.h — W5500 transport (proposal, not implemented) */
typedef enum { ETH_DOWN, ETH_CONNECTING, ETH_UP } eth_state_t;
void        eth_link_init(SPI_HandleTypeDef *hspi, const eth_endpoint_t *peer);
eth_state_t eth_link_tick(void);                  /* connection state machine */
bool        eth_link_send(const uint8_t *buf, uint16_t len);
int32_t     eth_link_recv(uint8_t *buf, uint16_t cap); /* 0 = nothing yet */
void        eth_link_drain(void);
void        eth_link_fail(void);                  /* close + backoff */
```

The existing Modbus policy (`send_request()`, `read_response()`, the
`IDLE` / `AWAITING_RESPONSE` logic) would sit on top and keep the
`plc_link_*` API, so `app_main.c` would not change for Modbus. A new protocol
would be a second policy module on the same transport. The W5500 facts the
transport must preserve are in [07-w5500.md](07-w5500.md).

### A different physical link

A protocol on a different medium (for example an RS-485 transceiver on a free
USART) does not need the W5500 code at all. Write a new module that implements
the contract in section 11.3 directly, with its own configuration header.
Pins and peripherals must come from the free resources listed in
[02-mcu-resources.md](02-mcu-resources.md); USART1 (PA9/PA10) is not free
because those pins are ZERO_MOTOR01 and ETH_RESET.

## 11.5 Selecting the protocol at build time

Follow the existing `BRAILLE_INPUT` pattern in `CMakeLists.txt`:

1. Add a value to the cache variable's accepted list and to the validation
   `if` that rejects unknown values.
2. Add a compile definition for it (`BRAILLE_INPUT_<NAME>`).
3. In `app_main.c`, add a branch that includes the new module and fills
   `app_reader_io.request_poll`, the module's `plc_link_io_t`-style table,
   the `init` call in `App_init()`, the `tick` call in `App_run()`, and the
   `on_exti` call if any.
4. If the new variant does not use the W5500, consider excluding `Lib/w5500`
   from that build the same way the MODBUS variant excludes `USB_DEVICE/`.
5. Add the definition to Doxygen's `PREDEFINED` if the variant should appear
   in the generated reference.

## 11.6 Testing a new protocol

- Keep the codec free of I/O, like `modbus_tcp.c`, so every validation path
  can be tested on the host with byte arrays (see `tests/test_modbus_tcp.c`
  and [13-build-test-debug.md](13-build-test-debug.md#host-tests)).
- The reader's behaviour needs no new tests: drive the new link against the
  existing `reader_ui` on hardware and check the sounds listed in
  [10-sound-feedback.md](10-sound-feedback.md).
- Provide a simulator for the bench, as `tools/plc_sim.py` does for Modbus.
