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

**None.** The vendored files are byte-for-byte what was fetched.

This upstream revision already guards both selection macros with
`#ifndef` in `wizchip_conf.h`:

```c
#ifndef _WIZCHIP_
#define _WIZCHIP_                      W6300   // default if nothing selects a chip
...
#endif
```

and, inside the `#elif (_WIZCHIP_ == W5500)` branch:

```c
#ifndef _WIZCHIP_IO_MODE_
#define _WIZCHIP_IO_MODE_           _WIZCHIP_IO_MODE_SPI_
#endif
```

Because both are already `#ifndef`-guarded, the `-D_WIZCHIP_=W5500` and
`-D_WIZCHIP_IO_MODE_=_WIZCHIP_IO_MODE_SPI_VDM_` compile definitions in
`Lib/w5500/CMakeLists.txt` take effect before the header's own `#define` is
reached, so its default is skipped rather than colliding. The brief's
fallback of wrapping the upstream `#define` by hand was not needed --
verified by reading the fetched header before writing the CMake target.

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
