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

// Unit conversion, so the rates below read as "one revolution per <time>".
#define APP_MS_PER_S 1000u

// Step rate that turns the disc once per APP_ROTATION_TIME_MS.
#define APP_STEP_RATE_HZ                                                       \
  (APP_MICROSTEPS_PER_REV * APP_MS_PER_S / APP_ROTATION_TIME_MS)

// Settling time the TMC2209 needs after its UART is bound, before the first
// register write.
#define APP_DRIVER_POWER_UP_MS 200u

// Both discs are geared so that the direction the TMC2209 calls forward turns
// them towards *decreasing* braille weight. The stepper port flips DIR for
// them, so everything above it keeps counting positive steps as increasing
// angle. Set this false for a motor rebuilt with the rotation the other way.
#define APP_MOTOR_DIR_INVERTED true

// LED blink codes: each motor reports faults as its own number in the
// schematic.
#define APP_MOTOR_01_BLINK_CODE 1u
#define APP_MOTOR_02_BLINK_CODE 2u

// Shape of a blink code: <code> short blinks, a long gap, repeated.
#define APP_WARNING_BURSTS 3u
#define APP_WARNING_ON_MS 100u
#define APP_WARNING_OFF_MS 200u
#define APP_WARNING_GAP_MS 800u

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
  HAL_Delay(APP_DRIVER_POWER_UP_MS);
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

// Report a startup fault without halting: <blinks> short blinks, a long
// pause, three times over, then carry on. Every caller passes the motor index
// so the pattern says which motor is affected.
static void blink_warning(uint32_t blinks) {
  for (uint32_t burst = 0u; burst < APP_WARNING_BURSTS; ++burst) {
    for (uint32_t i = 0u; i < blinks; ++i) {
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
      HAL_Delay(APP_WARNING_ON_MS);
      HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
      HAL_Delay(APP_WARNING_OFF_MS);
    }
    HAL_Delay(APP_WARNING_GAP_MS);
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
                         dir_pin, APP_MOTOR_DIR_INVERTED, NULL, 0u);
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

  // A driver that does not answer over UART still turns: the configuration
  // commands are all write-only, so only read-back verification and
  // diagnostics are lost. To fix it, check the PDN_UART wiring: TX through
  // ~1k into PDN_UART, RX tied directly to it.
  if (!motor_driver_init(&tmc_motor_01, &tmc_port_motor_01, huart_01->Instance,
                         ENABLE_MOTOR01_GPIO_Port, ENABLE_MOTOR01_Pin)) {
    blink_warning(APP_MOTOR_01_BLINK_CODE);
  }
  if (!motor_driver_init(&tmc_motor_02, &tmc_port_motor_02, huart_02->Instance,
                         ENABLE_MOTOR02_GPIO_Port, ENABLE_MOTOR02_Pin)) {
    blink_warning(APP_MOTOR_02_BLINK_CODE);
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

// ----------------------------------------------------------------------------
// Homing
//
// The ZERO sensors read high while a disc's flag sits over them. Every search
// below is bounded in microsteps, so a disconnected sensor or a jammed disc
// cannot spin forever inside App_init(), and the flag edge is always detected
// while turning in the same direction, which keeps the sensor's hysteresis
// out of the zero reference.
// ----------------------------------------------------------------------------

// A homing search covers at most a full revolution. Four seconds for that
// turn keeps startup short while staying well inside the motor's torque, and
// because the sensor is sampled once per microstep it costs no accuracy.
#define APP_HOMING_SEARCH_TIME_MS 4000u
#define APP_HOMING_SEEK_RATE_HZ                                                \
  (APP_MICROSTEPS_PER_REV * APP_MS_PER_S / APP_HOMING_SEARCH_TIME_MS)

// Final approach speed. The STEP timers cannot express much below one pulse
// per full counter range (see pulse_train_start() in stepper_hal.c, roughly
// 16 Hz with this clock tree), so keep clear of that floor.
#define APP_HOMING_FINE_RATE_HZ 25u

// Angular slack allowed around the flag: how far the back-off may travel
// looking for the sensor's release point, and the margin granted on top of a
// full revolution before a search is declared lost. Expressed as an angle so
// it survives a change of microstepping.
#define APP_HOMING_SLACK_TENTHS 450u
#define APP_HOMING_SLACK_STEPS                                                 \
  (APP_HOMING_SLACK_TENTHS * APP_MICROSTEPS_PER_REV / APP_ANGLE_TENTHS_PER_REV)

// A search that has turned a disc a full revolution plus the slack has missed
// the flag: the sensor is dead, miswired, or the disc is jammed.
#define APP_HOMING_SEARCH_STEPS                                                \
  (APP_MICROSTEPS_PER_REV + APP_HOMING_SLACK_STEPS)

// The final approach has to undo whatever the back-off travelled and still
// reach the edge, so it gets the back-off budget over again.
#define APP_HOMING_APPROACH_STEPS (APP_HOMING_SLACK_STEPS * 2u)

// Settling time between reversing direction and the final approach.
#define APP_HOMING_SETTLE_MS 50u

// Position assigned to a disc parked on its flag, i.e. the microstep that
// steps_to_angle() treats as angle 0. Make this non-zero if a flag turns out
// not to sit on the disc's pattern origin.
#define APP_ZERO_POSITION_STEPS 0

// Travel of a single homing step, in microsteps, in the planner's own frame
// (positive = increasing angle; the port maps that onto the DIR pin). Every
// flag edge is detected while turning in ::APP_HOMING_SEARCH_DIR, so the
// trigger point does not depend on which way the disc came from; the opposite
// direction is only used to leave the flag. Swapping the two signs reverses
// the physical sweep, which parks zero on the other edge of the flag.
#define APP_HOMING_SEARCH_DIR (1)
#define APP_HOMING_BACKOFF_DIR (-1)

// Step <stepper> one microstep at a time in <dir> until the sensor on
// <port>/<pin> reads <level>, at most <max_steps> microsteps. Returns false
// only when that budget ran out with the sensor still in the other state.
static bool seek_sensor(Stepper_t *stepper, GPIO_TypeDef *port, uint16_t pin,
                        GPIO_PinState level, int32_t dir, uint16_t rate_hz,
                        uint32_t max_steps) {
  while (HAL_GPIO_ReadPin(port, pin) != level) {
    if (max_steps == 0u) {
      return false;
    }
    --max_steps;
    start_move(stepper, dir, rate_hz);
    // Only this disc is moving, so wait on it alone rather than on both.
    while (Stepper_IsBusy(stepper)) {
    }
  }
  return true;
}

// Bring one disc onto its zero flag, parked on the edge that
// ::APP_HOMING_SEARCH_DIR reaches first. The caller assigns the position.
static bool home_disc(Stepper_t *stepper, GPIO_TypeDef *port, uint16_t pin) {
  // Walk off the flag first. A disc that powers up over its sensor would
  // otherwise be zeroed wherever inside the sensor window it happened to
  // stop, an error as wide as the flag itself.
  if (!seek_sensor(stepper, port, pin, GPIO_PIN_RESET, APP_HOMING_SEARCH_DIR,
                   APP_HOMING_SEEK_RATE_HZ, APP_HOMING_SEARCH_STEPS)) {
    return false;
  }
  // Search for the flag.
  if (!seek_sensor(stepper, port, pin, GPIO_PIN_SET, APP_HOMING_SEARCH_DIR,
                   APP_HOMING_SEEK_RATE_HZ, APP_HOMING_SEARCH_STEPS)) {
    return false;
  }
  // Back off until the sensor releases, then creep onto that same edge again
  // from the same side, so the trigger point does not depend on how fast the
  // disc arrived.
  if (!seek_sensor(stepper, port, pin, GPIO_PIN_RESET, APP_HOMING_BACKOFF_DIR,
                   APP_HOMING_SEEK_RATE_HZ, APP_HOMING_SLACK_STEPS)) {
    return false;
  }
  HAL_Delay(APP_HOMING_SETTLE_MS);
  return seek_sensor(stepper, port, pin, GPIO_PIN_SET, APP_HOMING_SEARCH_DIR,
                     APP_HOMING_FINE_RATE_HZ, APP_HOMING_APPROACH_STEPS);
}

bool calibrate_zero_position(void) {
  bool homed = true;

  if (home_disc(&stepper_motor_01, ZERO_MOTOR01_GPIO_Port, ZERO_MOTOR01_Pin)) {
    Stepper_SetPosition(&stepper_motor_01, APP_ZERO_POSITION_STEPS);
  } else {
    blink_warning(APP_MOTOR_01_BLINK_CODE);
    homed = false;
  }

  if (home_disc(&stepper_motor_02, ZERO_MOTOR02_GPIO_Port, ZERO_MOTOR02_Pin)) {
    Stepper_SetPosition(&stepper_motor_02, APP_ZERO_POSITION_STEPS);
  } else {
    blink_warning(APP_MOTOR_02_BLINK_CODE);
    homed = false;
  }

  return homed;
}
