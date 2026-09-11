# Vendored WIZnet ioLibrary_Driver

Source: https://github.com/Wiznet/ioLibrary_Driver
Commit: 39fae86465dbaa728107c3b2a90692c0a1639735 (upstream commit date 2026-06-18)
Fetched: 2026-09-09

## What was copied

Only `Ethernet/`:

- `Ethernet/wizchip_conf.{c,h}`
- `Ethernet/socket.{c,h}`
- `Ethernet/W5500/w5500.{c,h}`
- `license.txt` (upstream's licence file; this checkout uses that name, not `LICENSE`)

The `Internet/` tree is deliberately absent. This device has a static address
and speaks only MODBUS TCP, so DHCP, DNS, SNTP, HTTP and FTP would be dead
code on a 64 KB part. The other chip directories (W5100, W5100S, W5200,
W5300, W6100, W6300) are absent too; this board only ever has a W5500 wired
to it.

## Local modifications

### `Ethernet/socket.c` -- `recv()` test ordering

One change, found on the bench. In `recv()`, the non-IPv6 branch tested the
non-blocking flag *before* testing whether any data had arrived:

```c
if (sock_io_mode & (1 << sn)) return SOCK_BUSY;   /* upstream: first */
if (recvsize != 0) break;
```

`SOCK_BUSY` is `0`, so on a socket opened with `SF_IO_NONBLOCK` this returns 0
unconditionally -- even with a complete frame already sitting in the RX buffer.
A caller that treats a non-positive return as an error then destroys a perfectly
healthy connection on every read.

That is what it did here: the device connected, polled, received a valid MODBUS
reply, and sent an RST roughly 200 ms later, every cycle. Instrumenting the
firmware and reading it back over SWD showed `Sn_SR` = `0x17`
(`SOCK_ESTABLISHED`) with `recv()` returning `0` -- a healthy socket and a
refusing read.

The two tests are now in the other order, which is exactly what the
`IPV6_AVAILABLE` branch a few lines above already does. That branch is the
evidence this is a defect rather than a contract: upstream clearly intends
"data first, non-blocking second".

Only `recv()` is changed. `send()`, `connect()` and `recvfrom()` contain the
same shape in places but are not affected by it in the way this project uses
them, and are left untouched.


## API differences from the brief's assumptions

The brief was written against documentation, not this source. Checked every
divergence point it flagged, plus the full interface list Task 10 consumes,
against the fetched headers:

- **`reg_wizchip_spiburst_cbfunc`**: exists, and for every chip except W6100
  its signature is `(void (*)(uint8_t*, uint16_t), void (*)(uint8_t*, uint16_t))`
  -- exactly what the brief's `w5500_stm32.c` already assumed. No port code
  change was needed. (The `datasize_t` variant the brief warned about is real,
  but it only applies to the W6100's four-argument overload of
  `reg_wizchip_spi_cbfunc`, a different chip and a different function.)
- **`wizphy_getphylink`**: exists for W5500 (`#if _WIZCHIP_ > W5100`, and
  W5500 > W5100). Signature: `int8_t wizphy_getphylink(void)`. Returns
  `PHY_LINK_ON` (1) or `PHY_LINK_OFF` (0), reading `PHYCFGR` bit 0
  internally. No `PHYCFGR`-reading fallback was needed -- Task 10 should call
  `wizphy_getphylink() == PHY_LINK_ON` directly.
- **`wizchip_init` return convention**: confirmed from `wizchip_conf.c`.
  Every failure path returns exactly `-1`; the only success path returns
  exactly `0`. Task 10's `< 0` check is correct (and equivalent to `!= 0`
  for this implementation, since there is no positive return value).
- **`connect()` is a variadic macro, not a plain function.** This upstream
  revision added W6100/W6300 (IPv6) support, and `socket.h` now defines:

  ```c
  int8_t connect_W5x00(uint8_t sn, uint8_t * addr, uint16_t port);
  int8_t connect_W6x00(uint8_t sn, uint8_t * addr, uint16_t port, uint8_t addrlen);
  #define connect(...) CHOOSE_TESTCODE_MACRO(__VA_ARGS__)(__VA_ARGS__)
  ```

  which dispatches on argument count: a 3-argument call `connect(sn, addr,
  port)` expands to `connect_W5x00(sn, addr, port)`, matching the brief's
  assumed signature and call sites exactly. This is transparent for normal
  use but means `connect` cannot be used as a function pointer, and anyone
  grepping the header for `int8_t connect(` will not find it -- it is
  commented out (`//int8_t connect(uint8_t sn, uint8_t * addr, uint16_t port,
  uint8_t addrlen);` at socket.h:315) and superseded by the macro. `close`,
  `send`, `recv`, `getSn_SR`, `getSn_RX_RSR` are plain functions/macros as
  the brief assumed, not variadic.

No other divergences were found. See `task-9-report.md` for the full
confirmed name/signature list this vendoring hands to Task 10.

## Configuration

`_WIZCHIP_` = `W5500` and `_WIZCHIP_IO_MODE_` = `_WIZCHIP_IO_MODE_SPI_VDM_`,
set as `PUBLIC` compile definitions in `Lib/w5500/CMakeLists.txt`.
