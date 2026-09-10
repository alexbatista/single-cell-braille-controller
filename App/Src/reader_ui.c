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
