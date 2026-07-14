// ----------------------------------------------------------------------------
// stepper.h
//
// STEP/DIR stepper core. Platform independent: all pin access goes through
// the Stepper_Hal_t callbacks (same approach as tmc2209_hal_t in
// Lib/tmc2209), so this module compiles on the host for testing.
//
// Step timing (pulse rate, ramps) is owned by the caller — the motion
// planner or a timer interrupt — not by this module.
// ----------------------------------------------------------------------------

#ifndef STEPPER_H
#define STEPPER_H

#include <stdbool.h>
#include <stdint.h>

// Hardware access used by the stepper core. All callbacks receive the user
// supplied context pointer as their first argument.
typedef struct Stepper_Hal {
  // Drive the STEP output: true = high, false = low.
  void (*step_write)(void *ctx, bool level);

  // Drive the DIR output: true = forward, false = backward.
  void (*dir_write)(void *ctx, bool level);

  //Drive the ENABLE output: true = high, false = low.
  void (*enable_write)(void *ctx, bool level);

  // Opaque pointer passed unchanged to every callback.
  void *ctx;
} Stepper_Hal_t;

// Instance state. Treat members as private; initialize with Stepper_Init().
typedef struct Stepper {
  Stepper_Hal_t hal;
  int32_t position; // signed step count, updated when a pulse completes
  bool forward;
  bool enabled;
} Stepper_t;

// hal is copied into the instance; direction is set to forward and the STEP
// output is driven low. The GPIOs must already be configured as outputs.
void Stepper_Init(Stepper_t *stepper, Stepper_Hal_t const *hal);

void Stepper_SetDirection(Stepper_t *stepper, bool forward);

// Split pulse for use from a timer interrupt: Begin raises STEP, End lowers
// it and updates the position. The caller controls the time between the two
// calls and must respect the driver's minimum pulse width (TMC2209: 100 ns).
void Stepper_StepPulseBegin(Stepper_t *stepper);
void Stepper_StepPulseEnd(Stepper_t *stepper);

// Convenience full pulse (Begin immediately followed by End).
void Stepper_Step(Stepper_t *stepper);

int32_t Stepper_GetPosition(Stepper_t const *stepper);

// Redefine the current position (e.g. after homing) without moving.
void Stepper_SetPosition(Stepper_t *stepper, int32_t position);

#endif // STEPPER_H
