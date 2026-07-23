#include "stepper.h"

#include <stddef.h>

void Stepper_Init(Stepper_t *stepper, Stepper_Hal_t const *hal) {
  stepper->hal = *hal;
  stepper->position = 0;
  stepper->remaining = 0u;
  stepper->forward = true;
  stepper->enabled = false;
  stepper->hal.dir_write(stepper->hal.ctx, true);
  stepper->hal.pulse_train_stop(stepper->hal.ctx);
  if (stepper->hal.enable_write != NULL) {
    stepper->hal.enable_write(stepper->hal.ctx, false);
  }
}

void Stepper_Enable(Stepper_t *stepper) {
  stepper->enabled = true;
  if (stepper->hal.enable_write != NULL) {
    stepper->hal.enable_write(stepper->hal.ctx, true);
  }
}

void Stepper_Disable(Stepper_t *stepper) {
  stepper->enabled = false;
  if (stepper->hal.enable_write != NULL) {
    stepper->hal.enable_write(stepper->hal.ctx, false);
  }
}

bool Stepper_IsEnabled(Stepper_t const *stepper) { return stepper->enabled; }

void Stepper_SetDirection(Stepper_t *stepper, bool forward) {
  stepper->forward = forward;
  stepper->hal.dir_write(stepper->hal.ctx, forward);
}

void Stepper_MoveSteps(Stepper_t *stepper, uint32_t steps, uint32_t pulse_hz) {
  if (steps == 0u || pulse_hz == 0u || stepper->remaining != 0u) {
    return;
  }
  // Written before the train starts, so no interrupt can be in flight here.
  stepper->remaining = steps;
  stepper->hal.pulse_train_start(stepper->hal.ctx, pulse_hz);
}

void Stepper_Stop(Stepper_t *stepper) {
  stepper->remaining = 0u;
  stepper->hal.pulse_train_stop(stepper->hal.ctx);
}

bool Stepper_IsBusy(Stepper_t const *stepper) { return stepper->remaining != 0u; }

void Stepper_OnPulseComplete(Stepper_t *stepper) {
  // A pulse already in flight when the train was stopped still raises this
  // callback; it must not be counted against the next move.
  if (stepper->remaining == 0u) {
    return;
  }
  stepper->position += stepper->forward ? 1 : -1;
  if (--stepper->remaining == 0u) {
    stepper->hal.pulse_train_stop(stepper->hal.ctx);
  }
}

int32_t Stepper_GetPosition(Stepper_t const *stepper) {
  return stepper->position;
}

void Stepper_SetPosition(Stepper_t *stepper, int32_t position) {
  stepper->position = position;
}
