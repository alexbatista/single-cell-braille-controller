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

#include <stdbool.h>
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
