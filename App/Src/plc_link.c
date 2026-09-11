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

/** @brief Chunks drain_socket() will discard before giving up, so a chatty
 *         peer cannot hold the main loop. The W5500's RX buffer is 16 KB. */
#define PLC_DRAIN_MAX_CHUNKS 512u

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
  // Not const: ioLibrary takes uint8_t*, and casting the const away at the
  // call site would be a wart that reads like a bug.
  static uint8_t tx_sizes[8] = {PLC_SOCKET_BUFFER_KB, 0u, 0u, 0u,
                                0u,                   0u, 0u, 0u};
  static uint8_t rx_sizes[8] = {PLC_SOCKET_BUFFER_KB, 0u, 0u, 0u,
                                0u,                   0u, 0u, 0u};

  w5500_stm32_hard_reset();
  w5500_stm32_init(link.spi);

  if (getVERSIONR() != PLC_W5500_VERSION) {
    return false;
  }
  if (wizchip_init(tx_sizes, rx_sizes) < 0) {
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

// Discard everything queued on the socket. Called whenever the byte stream
// can no longer be trusted to start on a frame boundary: an oversized
// backlog, a frame that failed validation, or a request we stopped waiting
// for. Without this a desynced stream stays desynced, because nothing else
// reopens the socket while TCP itself is healthy.
static void drain_socket(void) {
  for (uint16_t chunks = 0u; chunks < PLC_DRAIN_MAX_CHUNKS; ++chunks) {
    uint16_t available = getSn_RX_RSR(PLC_SOCKET_NUMBER);
    if (available == 0u) {
      return;
    }
    if (available > (uint16_t)sizeof link.frame) {
      available = (uint16_t)sizeof link.frame;
    }
    if (recv(PLC_SOCKET_NUMBER, link.frame, available) <= 0) {
      return;
    }
  }
}

static void read_response(void) {
  uint16_t available = getSn_RX_RSR(PLC_SOCKET_NUMBER);
  if (available < MODBUS_READ_COILS_RSP_LEN(PLC_COIL_COUNT)) {
    return; // still arriving; the timeout in the caller bounds the wait
  }
  if (available > (uint16_t)sizeof link.frame) {
    // A backlog this large means the stream is already off a frame
    // boundary: there is nothing worth parsing, only something to discard
    // before the next request can resynchronise on a fresh boundary.
    drain_socket();
    report_fault();
    enter(PLC_LINK_IDLE);
    return;
  }

  int32_t received = recv(PLC_SOCKET_NUMBER, link.frame, available);
  if (received == 0) {
    // SOCK_BUSY on a non-blocking socket: the call consumed nothing. The
    // bytes are still queued and the connection is healthy, so retry on the
    // next tick instead of tearing down a link that has done nothing wrong.
    // The caller's response timeout bounds how long this can repeat.
    return;
  }
  if (received < 0) {
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
    // rather than tearing it down. Draining first guarantees the next
    // request starts back on a frame boundary instead of staying desynced.
    drain_socket();
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
    // An unplugged cable sits here until the PHY comes up -- there is
    // nothing to retry -- but the reader still needs to be told about it
    // periodically rather than only once, so silence is never mistaken for
    // a dead device.
    if (wizphy_getphylink() == PHY_LINK_ON) {
      if (socket(PLC_SOCKET_NUMBER, Sn_MR_TCP, PLC_LOCAL_PORT,
                 SF_IO_NONBLOCK) != (int8_t)PLC_SOCKET_NUMBER) {
        fail_to_backoff();
        return;
      }
      static uint8_t server_ip[4] = {PLC_SERVER_IP_0, PLC_SERVER_IP_1,
                                     PLC_SERVER_IP_2, PLC_SERVER_IP_3};
      // Non-blocking, so SOCK_BUSY here means "in progress", not "failed".
      (void)connect(PLC_SOCKET_NUMBER, server_ip, PLC_SERVER_PORT);
      enter(PLC_LINK_CONNECTING);
    } else if (elapsed_since(link.state_entered_ms, PLC_RECONNECT_DELAY_MS)) {
      // No cable: report roughly every reconnect delay instead of never.
      // reader_ui's own rate limiter, not this one, decides how often that
      // fault actually sounds.
      report_fault();
      enter(PLC_LINK_LINK_WAIT); // restamp the timer, stay here
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
    // Nothing consumes irq_pending: this tick runs on every main-loop pass,
    // so read_response() below is already as prompt as an interrupt could
    // make it. See the note on plc_link_on_exti() in the header.
    if (getSn_SR(PLC_SOCKET_NUMBER) != SOCK_ESTABLISHED) {
      fail_to_backoff();
      return;
    }
    read_response();
    if (link.state == PLC_LINK_AWAITING_RESPONSE &&
        elapsed_since(link.state_entered_ms, PLC_RESPONSE_TIMEOUT_MS)) {
      // The connection may well be fine and the PLC merely slow, but a reader
      // waiting on a value needs to be told something, and the next poll will
      // try again. Draining first means a reply that lands late cannot sit
      // in the buffer and coalesce with the next poll's response -- without
      // this, one recv() could absorb both and the parser would reject the
      // whole blob over the stale leading transaction id, discarding a
      // perfectly good trailing frame along with it.
      drain_socket();
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
