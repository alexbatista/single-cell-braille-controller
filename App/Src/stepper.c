#include "stepper.h"

void Stepper_Init(Stepper_t *stepper, Stepper_Hal_t const *hal) {
  stepper->hal = *hal;
  stepper->position = 0;
  stepper->forward = true;
  stepper->hal.dir_write(stepper->hal.ctx, true);
  stepper->hal.step_write(stepper->hal.ctx, false);
}

void Stepper_SetDirection(Stepper_t *stepper, bool forward) {
  stepper->forward = forward;
  stepper->hal.dir_write(stepper->hal.ctx, forward);
}

void Stepper_StepPulseBegin(Stepper_t *stepper) {
  stepper->hal.step_write(stepper->hal.ctx, true);
}

void Stepper_StepPulseEnd(Stepper_t *stepper) {
  stepper->hal.step_write(stepper->hal.ctx, false);
  stepper->position += stepper->forward ? 1 : -1;
}

void Stepper_Step(Stepper_t *stepper) {
  Stepper_StepPulseBegin(stepper);
  Stepper_StepPulseEnd(stepper);
}

int32_t Stepper_GetPosition(Stepper_t const *stepper) {
  return stepper->position;
}

void Stepper_SetPosition(Stepper_t *stepper, int32_t position) {
  stepper->position = position;
}
