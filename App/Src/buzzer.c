// ----------------------------------------------------------------------------
// buzzer.c
//
// Tone patterns for the five reader notifications. Pitch is just a period:
// ARR = tick/f - 1, and a compare of half the period is 50% duty. The counter
// tick is read from the clock tree at init (48 MHz SYSCLK, APB1 /2 and a
// prescaler of 48 give 1 MHz today), so a retuned clock keeps the pitches.
//
// The patterns are separated by texture as well as pitch. Two sounds that
// differ only in frequency are hard to tell apart on a small piezo, so
// "asking" is a rising pair, "received" is one longer tone, and "something is
// waiting" is a fast triple chirp.
// ----------------------------------------------------------------------------

#include "buzzer.h"

#include <stddef.h>

/**
 * @brief Timer-clock multiplier when the APB1 prescaler is not 1.
 *
 * TIM4 sits on APB1, whose timer clock is PCLK1 doubled whenever the APB1
 * prescaler divides (RM0008 clock tree).
 */
#define BUZZER_APB1_TIMER_CLOCK_MULTIPLIER 2u

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
static uint32_t buzzer_tick_hz;

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
  uint32_t period = (buzzer_tick_hz / (uint32_t)frequency_hz) - 1u;
  __HAL_TIM_SET_AUTORELOAD(buzzer_timer, period);
  __HAL_TIM_SET_COMPARE(buzzer_timer, buzzer_channel,
                        (period + 1u) / BUZZER_DUTY_DIVISOR);
  __HAL_TIM_SET_COUNTER(buzzer_timer, 0u);
  (void)HAL_TIM_PWM_Start(buzzer_timer, buzzer_channel);
}

// Frequency the buzzer timer counts at, after its prescaler, derived the same
// way as the STEP timers' tick in stepper_hal.c.
static uint32_t buzzer_timer_tick_hz(TIM_HandleTypeDef const *htim) {
  uint32_t tick_hz = HAL_RCC_GetPCLK1Freq();
  if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1) {
    tick_hz *= BUZZER_APB1_TIMER_CLOCK_MULTIPLIER;
  }
  return tick_hz / (htim->Instance->PSC + 1u);
}

void buzzer_init(TIM_HandleTypeDef *htim, uint32_t channel) {
  buzzer_timer = htim;
  buzzer_channel = channel;
  buzzer_tick_hz = buzzer_timer_tick_hz(htim);
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
