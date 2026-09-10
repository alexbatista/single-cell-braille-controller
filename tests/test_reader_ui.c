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
