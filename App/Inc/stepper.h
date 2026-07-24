/**
 * @file    stepper.h
 * @brief   STEP/DIR stepper core (platform independent).
 * @author  Alex Batista (alexbatista.asb@gmail.com)
 *
 * Platform independent stepper control: all hardware access goes through the
 * ::Stepper_Hal_t callbacks (same approach as `tmc2209_hal_t` in
 * Lib/tmc2209), so this module compiles on the host for testing.
 *
 * Pulses are produced by a hardware pulse train (a PWM output on STM32), not
 * by the CPU: the caller asks for a number of steps at a rate, the port
 * starts the train, and one interrupt per emitted pulse feeds
 * ::Stepper_OnPulseComplete() until the requested count is reached. The pulse
 * rate is chosen by the caller (the motion planner); this module only owns
 * the step budget and the position.
 */

#ifndef STEPPER_H
#define STEPPER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @defgroup stepper Stepper Core
 * @brief    Platform-independent STEP/DIR stepper control.
 * @{
 */

/**
 * @brief Hardware access used by the stepper core.
 *
 * All callbacks receive the user supplied context pointer (::Stepper_Hal_t::ctx)
 * as their first argument. Populate this via a port such as
 * Stepper_Hal_Stm32_Init().
 */
typedef struct Stepper_Hal {
  /** @brief Drive the DIR output: true = forward, false = backward. */
  void (*dir_write)(void *ctx, bool level);

  /**
   * @brief Drive the driver enable input (optional, may be NULL).
   *
   * @p enable true = driver enabled (motor energized), false = disabled. The
   * port implements any inversion the driver needs (the TMC2209 ENN input is
   * active low).
   */
  void (*enable_write)(void *ctx, bool enable);

  /**
   * @brief Start emitting STEP pulses at @p pulse_hz and keep going until
   *        stopped.
   *
   * The port must raise one ::Stepper_OnPulseComplete() call per emitted pulse.
   */
  void (*pulse_train_start)(void *ctx, uint32_t pulse_hz);

  /**
   * @brief Stop the pulse train.
   * @note  Must be safe to call when no train is running.
   */
  void (*pulse_train_stop)(void *ctx);

  void *ctx; /**< Opaque pointer passed unchanged to every callback. */
} Stepper_Hal_t;

/**
 * @brief Stepper instance state.
 *
 * Treat members as private; initialize with Stepper_Init().
 */
typedef struct Stepper {
  Stepper_Hal_t hal;           /**< Hardware access callbacks (copied in). */
  int32_t position;            /**< Signed step count, updated per emitted pulse. */
  volatile uint32_t remaining; /**< Steps left in the current move, 0 when idle. */
  bool forward;                /**< Current direction: true = forward. */
  bool enabled;                /**< Driver enable flag. */
} Stepper_t;

/**
 * @brief Initialize a stepper instance.
 *
 * @p hal is copied into the instance; direction is set to forward, the pulse
 * train is stopped and the driver starts disabled.
 *
 * @param stepper Instance to initialize.
 * @param hal     Hardware access callbacks; copied into @p stepper.
 *
 * @pre The DIR pin must already be configured as an output and the STEP timer
 *      must already be initialized.
 */
void Stepper_Init(Stepper_t *stepper, Stepper_Hal_t const *hal);

/**
 * @brief Enable the driver.
 *
 * Drives the enable output through the optional ::Stepper_Hal_t::enable_write
 * callback; only the enabled flag changes when the HAL does not provide one.
 *
 * @param stepper Instance to enable.
 */
void Stepper_Enable(Stepper_t *stepper);

/**
 * @brief Disable the driver.
 * @param stepper Instance to disable.
 * @see   Stepper_Enable()
 */
void Stepper_Disable(Stepper_t *stepper);

/**
 * @brief Query whether the driver is enabled.
 * @param stepper Instance to query.
 * @return true when the driver is enabled.
 */
bool Stepper_IsEnabled(Stepper_t const *stepper);

/**
 * @brief Set the step direction.
 *
 * Takes effect on the next move; the position count follows this flag.
 *
 * @param stepper Instance to configure.
 * @param forward true = forward, false = backward.
 */
void Stepper_SetDirection(Stepper_t *stepper, bool forward);

/**
 * @brief Start a background move of @p steps pulses at @p pulse_hz.
 *
 * Emits @p steps pulses at @p pulse_hz in the current direction and returns
 * immediately: the move completes in the background, one pulse per
 * ::Stepper_OnPulseComplete() call. Poll Stepper_IsBusy() to wait for it.
 *
 * @param stepper  Instance to move.
 * @param steps    Number of pulses to emit.
 * @param pulse_hz Pulse rate in Hz.
 *
 * @note Does nothing when @p steps or @p pulse_hz is zero, or when a move is
 *       already running (call Stepper_Stop() first to override).
 */
void Stepper_MoveSteps(Stepper_t *stepper, uint32_t steps, uint32_t pulse_hz);

/**
 * @brief Abort the current move.
 *
 * The position keeps whatever was already stepped.
 *
 * @param stepper Instance to stop.
 */
void Stepper_Stop(Stepper_t *stepper);

/**
 * @brief Query whether a move is in progress.
 * @param stepper Instance to query.
 * @return true while a move is running.
 */
bool Stepper_IsBusy(Stepper_t const *stepper);

/**
 * @brief Notify the core that one STEP pulse was emitted.
 *
 * Called by the port once per emitted pulse, normally from an interrupt:
 * advances the position and stops the train on the last step.
 *
 * @param stepper Instance the pulse belongs to.
 */
void Stepper_OnPulseComplete(Stepper_t *stepper);

/**
 * @brief Read the current position.
 * @param stepper Instance to query.
 * @return Signed step count.
 */
int32_t Stepper_GetPosition(Stepper_t const *stepper);

/**
 * @brief Redefine the current position without moving (e.g. after homing).
 *
 * @param stepper  Instance to update.
 * @param position New position value in steps.
 */
void Stepper_SetPosition(Stepper_t *stepper, int32_t position);

/** @} */ // end of stepper

#endif // STEPPER_H
