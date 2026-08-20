#include "stepper_hal.h"

#include <stddef.h>

// TIM2 and TIM3 are 16-bit timers: ARR and CCR only hold 0..65535, so one
// pulse every 2^16 ticks is the longest period the hardware can express.
#define STEP_TIMER_COUNTER_BITS 16u
#define STEP_TIMER_MAX_PERIOD_TICKS (1uL << STEP_TIMER_COUNTER_BITS)

// Shortest period that still leaves a high time to pulse with.
#define STEP_TIMER_MIN_PERIOD_TICKS 2u

// STEP runs at a 50% duty cycle: the compare match halfway through the period
// ends the pulse, and that match is what raises the pulse callback.
#define STEP_PULSE_DUTY_DIVISOR 2u

// An APB1 prescaler other than 1 doubles the timer clock with respect to
// PCLK1 (RM0008 clock tree).
#define APB1_TIMER_CLOCK_MULTIPLIER 2u

static void gpio_write(GPIO_TypeDef *gpio_port, uint16_t pin_mask, bool level) {
  if (level) {
    gpio_port->BSRR = pin_mask;
  } else {
    gpio_port->BSRR = (uint32_t)pin_mask << 16u;
  }
}

// Frequency the STEP timer counts at, after its prescaler. Both STEP timers
// (TIM2, TIM3) sit on APB1, whose timer clock is PCLK1 doubled whenever the
// APB1 prescaler is not 1 — deriving it here instead of hardcoding keeps the
// pulse rate correct if the clock tree is retuned in CubeMX.
static uint32_t timer_tick_hz(TIM_HandleTypeDef const *tim) {
  uint32_t tick_hz = HAL_RCC_GetPCLK1Freq();
  if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_CFGR_PPRE1_DIV1) {
    tick_hz *= APB1_TIMER_CLOCK_MULTIPLIER;
  }
  return tick_hz / (tim->Instance->PSC + 1u);
}

static void pulse_train_start(void *ctx, uint32_t pulse_hz) {
  Stepper_Hal_Stm32_t *port = ctx;

  // The lower clamp caps the rate at half the timer tick rate rather than
  // dividing by zero. The upper clamp matters just as much: a rate slower
  // than one pulse per full counter range wraps ARR and CCR independently and
  // can land CCR above ARR, which never raises a compare match, so STEP would
  // stay high and the pulse callback would never fire, hanging every caller
  // that waits on Stepper_IsBusy(). Clamping turns that into a move that is
  // merely slower than asked for.
  uint32_t ticks = timer_tick_hz(port->step_tim) / pulse_hz;
  if (ticks > STEP_TIMER_MAX_PERIOD_TICKS) {
    ticks = STEP_TIMER_MAX_PERIOD_TICKS;
  } else if (ticks < STEP_TIMER_MIN_PERIOD_TICKS) {
    ticks = STEP_TIMER_MIN_PERIOD_TICKS;
  }

  __HAL_TIM_SET_AUTORELOAD(port->step_tim, ticks - 1u);
  __HAL_TIM_SET_COMPARE(port->step_tim, port->step_channel,
                        ticks / STEP_PULSE_DUTY_DIVISOR);

  // ARR and CCR are both preloaded (AutoReloadPreload in MX_TIMx_Init, OCxPE
  // set by HAL_TIM_PWM_ConfigChannel), so their shadow registers only load on
  // an update event. Force one now, otherwise the first pulse would still use
  // the previous period. It also resets the counter, so the train starts with
  // a full pulse.
  HAL_TIM_GenerateEvent(port->step_tim, TIM_EVENTSOURCE_UPDATE);

  // Drop the flags that update event just raised; a pending compare flag here
  // would fire a phantom pulse callback the moment the interrupt is enabled.
  port->step_tim->Instance->SR = 0u;

  // PWM mode 1 with the counter at 0 drives STEP high immediately and the
  // compare match ends the pulse, which is where the counting interrupt
  // fires: one callback per completed pulse.
  HAL_TIM_PWM_Start_IT(port->step_tim, port->step_channel);
}

static void pulse_train_stop(void *ctx) {
  Stepper_Hal_Stm32_t *port = ctx;
  // Disabling the channel hands the pin back to the GPIO output register,
  // which is 0 out of reset, so STEP idles low as the TMC2209 expects.
  HAL_TIM_PWM_Stop_IT(port->step_tim, port->step_channel);
}

static void dir_write(void *ctx, bool level) {
  Stepper_Hal_Stm32_t *port = ctx;
  // A mirrored motor is corrected at the pin, so every layer above still
  // counts positive steps as increasing angle.
  gpio_write(port->dir_port, port->dir_pin, port->invert_dir ? !level : level);
}

static void enable_write(void *ctx, bool enable) {
  Stepper_Hal_Stm32_t *port = ctx;
  // The TMC2209 ENN input is active low: drive the pin low to enable
  gpio_write(port->enable_port, port->enable_pin, !enable);
}

void Stepper_Hal_Stm32_Init(Stepper_Hal_Stm32_t *port, Stepper_Hal_t *hal,
                            TIM_HandleTypeDef *step_tim, uint32_t step_channel,
                            GPIO_TypeDef *dir_port, uint16_t dir_pin,
                            bool invert_dir, GPIO_TypeDef *enable_port,
                            uint16_t enable_pin) {
  port->step_tim = step_tim;
  port->step_channel = step_channel;
  port->dir_port = dir_port;
  port->dir_pin = dir_pin;
  port->invert_dir = invert_dir;
  port->enable_port = enable_port;
  port->enable_pin = enable_pin;

  hal->dir_write = dir_write;
  hal->enable_write = (enable_port != NULL) ? enable_write : NULL;
  hal->pulse_train_start = pulse_train_start;
  hal->pulse_train_stop = pulse_train_stop;
  hal->ctx = port;
}
