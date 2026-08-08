// ----------------------------------------------------------------------------
// motion_planner.c
//
// Motion planner core
// ----------------------------------------------------------------------------
#include "main.h"
#include "stepper.h"
#include "stepper_hal.h"
#include "motion_planner.h"
#include "stm32f103xb.h"
#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_tim.h"
#include "tmc2209.h"
#include "tmc2209_stm32.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_uart.h"
#include <stdint.h>

// Each motor is a TMC2209 configured over its own UART and moved through its
// STEP/DIR pins via the stepper module; both motors step in lockstep from
// the same paced loop.
// ----------------------------------------------------------------------------

#define APP_FULL_STEPS_PER_REV 200u // 1.8 degree motor
#define APP_MICROSTEPS 8u
#define APP_MICROSTEPS_PER_REV (APP_FULL_STEPS_PER_REV * APP_MICROSTEPS)
#define APP_ROTATION_TIME_MS 1000u
#define APP_RUN_CURRENT_PERCENT 25u
#define APP_HOLD_CURRENT_PERCENT 25u

// Step rate that turns the disc once per APP_ROTATION_TIME_MS.
#define APP_STEP_RATE_HZ (APP_MICROSTEPS_PER_REV * 1000u / APP_ROTATION_TIME_MS)

// Disc angles are expressed in tenths of a degree (see disk_angles_t).
#define APP_ANGLE_TENTHS_PER_REV 3600u

// Motor 01: TMC2209 on USART2 (PA2/PA3), ENN PA5, STEP PA1 (TIM2_CH2),
// DIR PA4.
static tmc2209_stm32_t tmc_port_motor_01;
static tmc2209_t tmc_motor_01;
static Stepper_Hal_Stm32_t stepper_hal_motor_01;
static Stepper_t stepper_motor_01;

// Motor 02: TMC2209 on USART3 (PB10/PB11), ENN PB0, STEP PB1 (TIM3_CH4),
// DIR PA7.
static tmc2209_stm32_t tmc_port_motor_02;
static tmc2209_t tmc_motor_02;
static Stepper_Hal_Stm32_t stepper_hal_motor_02;
static Stepper_t stepper_motor_02;

// Bring one TMC2209 up in UART mode and leave it enabled: bind the HAL to
// the UART peripheral and ENN pin, load safe defaults, then set the demo
// microstepping and motor currents. Returns false when the driver does not
// answer over UART (reads fail or the UART-mode config did not stick).
static bool motor_driver_init(tmc2209_t *tmc, tmc2209_stm32_t *tmc_port,
                              USART_TypeDef *usart, GPIO_TypeDef *enable_port,
                              uint16_t enable_pin) {
  tmc2209_hal_t hal;
  tmc2209_stm32_hal_init(tmc_port, &hal, usart, enable_port, enable_pin);
  HAL_Delay(200u);
  tmc2209_setup(tmc, &hal, TMC2209_SERIAL_ADDRESS_0);

  // tmc2209_setup() leaves automatic current scaling off; in stealthChop
  // (the mode the driver runs in) that means the coils only get the fixed
  // PWM_OFS amplitude (~14% of supply) and IRUN/IHOLD are ignored, which
  // makes the motor very weak. Re-enable the closed current loop so the
  // run/hold currents below actually set the torque.
  tmc2209_enable_automatic_current_scaling(tmc);
  tmc2209_enable_automatic_gradient_adaptation(tmc);

  tmc2209_set_microsteps_per_step(tmc, APP_MICROSTEPS);
  tmc2209_set_run_current(tmc, APP_RUN_CURRENT_PERCENT);
  tmc2209_set_hold_current(tmc, APP_HOLD_CURRENT_PERCENT);
  tmc2209_enable(tmc);

  return tmc2209_is_setup_and_communicating(tmc);
}

// Warn, without halting, when a TMC2209 does not answer over UART:
// <motor_index> short blinks, long pause, three times, then continue. The
// configuration commands are all write-only, so the motor still runs; only
// read-back verification and diagnostics are lost. To fix it, check the
// PDN_UART wiring: TX through ~1k into PDN_UART, RX tied directly to it.
static void motor_comm_warning(uint32_t motor_index) {
  for (uint32_t burst = 0u; burst < 3u; ++burst) {
    for (uint32_t i = 0u; i < motor_index; ++i) {
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
      HAL_Delay(100u);
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
      HAL_Delay(200u);
    }
    HAL_Delay(800u);
  }
}

// Bind a stepper instance to its DIR pin and to the timer channel that
// drives the STEP output. The ENN pin already belongs to the motor's tmc2209
// instance, so the stepper port gets no enable pin.
static void motor_stepper_init(Stepper_t *stepper,
                               Stepper_Hal_Stm32_t *stepper_port,
                               TIM_HandleTypeDef *step_tim,
                               uint32_t step_channel, GPIO_TypeDef *dir_port,
                               uint16_t dir_pin) {
  Stepper_Hal_t hal;
  Stepper_Hal_Stm32_Init(stepper_port, &hal, step_tim, step_channel, dir_port,
                         dir_pin, NULL, 0u);
  Stepper_Init(stepper, &hal);
}

// One STEP pulse just finished on one of the motor timers. The steppers are
// owned here, so the timer to instance mapping lives here too and the
// stepper port keeps no back pointer into the core.
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == TIM2) {
    Stepper_OnPulseComplete(&stepper_motor_01);
  } else if (htim->Instance == TIM3) {
    Stepper_OnPulseComplete(&stepper_motor_02);
  }
}

void initialize_motors(UART_HandleTypeDef *huart_01,
                       UART_HandleTypeDef *huart_02, TIM_HandleTypeDef *htim_01,
                       TIM_HandleTypeDef *htim_02) {
  // MX_GPIO_Init() already boots both ENN pins high (drivers disabled);
  // assert that state again here so App_init() is safe even if the CubeMX
  // defaults regress. Each motor is re-enabled at the end of its own
  // configuration.
  HAL_GPIO_WritePin(ENABLE_MOTOR01_GPIO_Port, ENABLE_MOTOR01_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(ENABLE_MOTOR02_GPIO_Port, ENABLE_MOTOR02_Pin, GPIO_PIN_SET);

  if (!motor_driver_init(&tmc_motor_01, &tmc_port_motor_01, huart_01->Instance,
                         ENABLE_MOTOR01_GPIO_Port, ENABLE_MOTOR01_Pin)) {
    motor_comm_warning(1u);
  }
  if (!motor_driver_init(&tmc_motor_02, &tmc_port_motor_02, huart_02->Instance,
                         ENABLE_MOTOR02_GPIO_Port, ENABLE_MOTOR02_Pin)) {
    motor_comm_warning(2u);
  }

  motor_stepper_init(&stepper_motor_01, &stepper_hal_motor_01, htim_01,
                     TIM_CHANNEL_2, DIR_MOTOR01_GPIO_Port, DIR_MOTOR01_Pin);
  motor_stepper_init(&stepper_motor_02, &stepper_hal_motor_02, htim_02,
                     TIM_CHANNEL_4, DIR_MOTOR02_GPIO_Port, DIR_MOTOR02_Pin);

  // Make sure both drivers follow the STEP/DIR interface (VACTUAL = 0).
  tmc2209_move_using_step_dir_interface(&tmc_motor_01);
  tmc2209_move_using_step_dir_interface(&tmc_motor_02);
}

// Start a move of steps microsteps, negative meaning backwards. The pulses
// are emitted by the STEP timer in the background; the caller waits with
// wait_until_idle().
static void start_move(Stepper_t *stepper, int32_t steps,
                       uint16_t step_rate_hz) {
  if (steps == 0) {
    return;
  }
  Stepper_SetDirection(stepper, steps > 0);
  Stepper_MoveSteps(stepper, (uint32_t)(steps > 0 ? steps : -steps),
                    step_rate_hz);
}

static void wait_until_idle(void) {
  while (Stepper_IsBusy(&stepper_motor_01) ||
         Stepper_IsBusy(&stepper_motor_02)) {
  }
}

// Shortest signed microstep move from the current position to an absolute
// disc angle. The discs turn freely, so a target more than half a turn ahead
// is reached faster by going backwards.
static int32_t steps_to_angle(Stepper_t const *stepper, uint16_t angle_tenths) {
  int32_t const per_rev = (int32_t)APP_MICROSTEPS_PER_REV;
  int32_t const target =
      (int32_t)(((uint32_t)angle_tenths * APP_MICROSTEPS_PER_REV) /
                APP_ANGLE_TENTHS_PER_REV);

  int32_t current = Stepper_GetPosition(stepper) % per_rev;
  if (current < 0) {
    current += per_rev;
  }

  int32_t delta = (target - current) % per_rev;
  if (delta > per_rev / 2) {
    delta -= per_rev;
  } else if (delta < -per_rev / 2) {
    delta += per_rev;
  }
  return delta;
}

// Turn both discs to their absolute angles and block until they get there;
// they run concurrently, so the move takes as long as the slower disc.
void move_to_angle(disk_angles_t disk_angle) {
  start_move(&stepper_motor_01,
             steps_to_angle(&stepper_motor_01, disk_angle.single_row_disc),
             APP_STEP_RATE_HZ);
  start_move(&stepper_motor_02,
             steps_to_angle(&stepper_motor_02, disk_angle.double_row_disc),
             APP_STEP_RATE_HZ);
  wait_until_idle();
}

// Turn both motors one full revolution over APP_ROTATION_TIME_MS, blocking.
// clockwise maps to DIR high; if a motor spins the other way, swap one of
// its coil pairs or use tmc2209_enable_inverse_motor_direction().
static void rotate_one_revolution(bool clockwise, uint16_t step_rate_hz) {
  int32_t const steps = clockwise ? (int32_t)APP_MICROSTEPS_PER_REV
                                  : -(int32_t)APP_MICROSTEPS_PER_REV;
  start_move(&stepper_motor_01, steps, step_rate_hz);
  start_move(&stepper_motor_02, steps, step_rate_hz);
  wait_until_idle();
}

void calibrate_zero_position(void) {
  while (HAL_GPIO_ReadPin(ZERO_MOTOR01_GPIO_Port, ZERO_MOTOR01_Pin) ==
         GPIO_PIN_RESET) {
    start_move(&stepper_motor_01, 1, APP_STEP_RATE_HZ / 20);
    wait_until_idle();
  }
  HAL_Delay(1000);
  while (HAL_GPIO_ReadPin(ZERO_MOTOR01_GPIO_Port, ZERO_MOTOR01_Pin) ==
         GPIO_PIN_RESET) {
    start_move(&stepper_motor_01, -1, APP_STEP_RATE_HZ / 800);
    wait_until_idle();
  }

  while (HAL_GPIO_ReadPin(ZERO_MOTOR02_GPIO_Port, ZERO_MOTOR02_Pin) ==
         GPIO_PIN_RESET) {
    start_move(&stepper_motor_02, 1, APP_STEP_RATE_HZ / 20);
    wait_until_idle();
  }
  HAL_Delay(1000);
  while (HAL_GPIO_ReadPin(ZERO_MOTOR02_GPIO_Port, ZERO_MOTOR02_Pin) ==
         GPIO_PIN_RESET) {
    start_move(&stepper_motor_02, -1, APP_STEP_RATE_HZ / 800);
    wait_until_idle();
  }
}