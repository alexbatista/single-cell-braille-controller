// ----------------------------------------------------------------------------
// stepper.h
//
// STEP/DIR stepper core. Platform independent: all hardware access goes
// through the Stepper_Hal_t callbacks (same approach as tmc2209_hal_t in
// Lib/tmc2209), so this module compiles on the host for testing.
//
// Pulses are produced by a hardware pulse train (a PWM output on STM32), not
// by the CPU: the caller asks for a number of steps at a rate, the port
// starts the train, and one interrupt per emitted pulse feeds
// Stepper_OnPulseComplete() until the requested count is reached. The pulse
// rate is chosen by the caller (the motion planner); this module only owns
// the step budget and the position.
// ----------------------------------------------------------------------------

#ifndef STEPPER_H
#define STEPPER_H

#include <stdbool.h>
#include <stdint.h>

// Hardware access used by the stepper core. All callbacks receive the user
// supplied context pointer as their first argument.
typedef struct Stepper_Hal {
  // Drive the DIR output: true = forward, false = backward.
  void (*dir_write)(void *ctx, bool level);

  // Optional, may be NULL. Drive the driver enable input: true = driver
  // enabled (motor energized), false = disabled. The port implements any
  // inversion the driver needs (the TMC2209 ENN input is active low).
  void (*enable_write)(void *ctx, bool enable);

  // Start emitting STEP pulses at pulse_hz and keep going until stopped. The
  // port must raise one Stepper_OnPulseComplete() call per emitted pulse.
  void (*pulse_train_start)(void *ctx, uint32_t pulse_hz);

  // Stop the pulse train. Must be safe to call when no train is running.
  void (*pulse_train_stop)(void *ctx);

  // Opaque pointer passed unchanged to every callback.
  void *ctx;
} Stepper_Hal_t;

// Instance state. Treat members as private; initialize with Stepper_Init().
typedef struct Stepper {
  Stepper_Hal_t hal;
  int32_t position;           // signed step count, updated per emitted pulse
  volatile uint32_t remaining; // steps left in the current move, 0 when idle
  bool forward;
  bool enabled;
} Stepper_t;

// hal is copied into the instance; direction is set to forward, the pulse
// train is stopped and the driver starts disabled. The DIR pin must already
// be configured as an output and the STEP timer must already be initialized.
void Stepper_Init(Stepper_t *stepper, Stepper_Hal_t const *hal);

// Drive the enable output through the optional enable_write callback; only
// the enabled flag changes when the HAL does not provide one.
void Stepper_Enable(Stepper_t *stepper);
void Stepper_Disable(Stepper_t *stepper);
bool Stepper_IsEnabled(Stepper_t const *stepper);

// Takes effect on the next move; the position count follows this flag.
void Stepper_SetDirection(Stepper_t *stepper, bool forward);

// Emit steps pulses at pulse_hz in the current direction and return
// immediately: the move completes in the background, one pulse per
// Stepper_OnPulseComplete() call. Poll Stepper_IsBusy() to wait for it.
// Does nothing when steps or pulse_hz is zero, or when a move is already
// running (call Stepper_Stop() first to override).
void Stepper_MoveSteps(Stepper_t *stepper, uint32_t steps, uint32_t pulse_hz);

// Abort the current move; the position keeps whatever was already stepped.
void Stepper_Stop(Stepper_t *stepper);

bool Stepper_IsBusy(Stepper_t const *stepper);

// Called by the port once per emitted pulse, normally from an interrupt:
// advances the position and stops the train on the last step.
void Stepper_OnPulseComplete(Stepper_t *stepper);

int32_t Stepper_GetPosition(Stepper_t const *stepper);

// Redefine the current position (e.g. after homing) without moving.
void Stepper_SetPosition(Stepper_t *stepper, int32_t position);

#endif // STEPPER_H
