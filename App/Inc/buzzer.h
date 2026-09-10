/**
 * @file    buzzer.h
 * @brief   Notification tones on the piezo buzzer.
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * The reader cannot see the cell, so every event that changes what the discs
 * mean has to be audible. Each sound is a short pattern of PWM tones,
 * separated from the others by pitch and by texture -- a rising pair, a
 * single tone, a triple chirp -- so they stay apart through a small piezo in
 * a noisy room.
 */

#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

#include "reader_ui.h"
#include "stm32f1xx_hal.h"

/**
 * @defgroup buzzer Buzzer
 * @brief    Audible notifications for the reader.
 * @{
 */

/**
 * @brief Bind the buzzer to its timer channel.
 *
 * The timer must already be configured for PWM with a 1 MHz counter tick,
 * which is what @c MX_TIM4_Init() sets up (prescaler 47 on the 48 MHz APB1
 * timer clock). Only the period and compare value are changed afterwards.
 *
 * @param htim    Timer driving the buzzer pin.
 * @param channel Timer channel, e.g. @c TIM_CHANNEL_1.
 */
void buzzer_init(TIM_HandleTypeDef *htim, uint32_t channel);

/**
 * @brief Play one notification and return when it has finished.
 *
 * Blocking, and deliberately so. PWM keeps sounding in hardware with no
 * software involvement, so a tone started immediately before a blocking disc
 * move would sound for the whole move unless something stopped it. Every
 * pattern is short -- 480 ms at the very worst, most under 250 ms -- against
 * disc moves that already block for around a second, and button presses are
 * latched by their interrupt throughout, so nothing is lost.
 *
 * @param sound Which notification to play. @c READER_SOUND_NONE and any
 *              unknown value do nothing.
 */
void buzzer_play(reader_sound_t sound);

/** @} */ // end of buzzer

#endif // BUZZER_H
