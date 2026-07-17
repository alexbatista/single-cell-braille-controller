#include "app_main.h"
#include "main.h"
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_uart.h"
#include "usart.h"

#include "stepper.h"
#include "stepper_hal.h"
#include "tmc2209.h"
#include "tmc2209_stm32.h"

// ----------------------------------------------------------------------------
// Demo motion: both motors turn clockwise indefinitely at one full mechanical
// revolution per 5 s (App_run() executes one revolution per call and is
// called forever from the main loop; the LED toggles once per revolution).
//
// Each motor is a TMC2209 configured over its own UART and moved through its
// STEP/DIR pins via the stepper module; both motors step in lockstep from
// the same paced loop.
// ----------------------------------------------------------------------------

#define APP_FULL_STEPS_PER_REV 200u // 1.8 degree motor
#define APP_MICROSTEPS 8u
#define APP_MICROSTEPS_PER_REV (APP_FULL_STEPS_PER_REV * APP_MICROSTEPS)
#define APP_ROTATION_TIME_MS 5000u

#define APP_RUN_CURRENT_PERCENT 40u
#define APP_HOLD_CURRENT_PERCENT 40u

// Motor 01: TMC2209 on USART2 (PA2/PA3), ENN PA5, STEP PA1, DIR PA4.
static tmc2209_stm32_t tmc_port_motor_01;
static tmc2209_t tmc_motor_01;
static Stepper_Hal_Stm32_t stepper_hal_motor_01;
static Stepper_t stepper_motor_01;

// Motor 02: TMC2209 on USART3 (PB10/PB11), ENN PB0, STEP PB1, DIR PA7.
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
  HAL_Delay(500u);
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

// Bind a stepper instance to its STEP/DIR pins. The ENN pin already belongs
// to the motor's tmc2209 instance, so the stepper port gets no enable pin.
static void motor_stepper_init(Stepper_t *stepper,
                               Stepper_Hal_Stm32_t *stepper_port,
                               GPIO_TypeDef *step_port, uint16_t step_pin,
                               GPIO_TypeDef *dir_port, uint16_t dir_pin) {
  Stepper_Hal_t hal;
  Stepper_Hal_Stm32_Init(stepper_port, &hal, step_port, step_pin, dir_port,
                         dir_pin, NULL, 0u);
  Stepper_Init(stepper, &hal);
}

void App_init(UART_HandleTypeDef *huart_01, UART_HandleTypeDef *huart_02) {
  // MX_GPIO_Init() already boots both ENN pins high (drivers disabled);
  // assert that state again here so App_init() is safe even if the CubeMX
  // defaults regress. Each motor is re-enabled at the end of its own
  // configuration.
  HAL_GPIO_WritePin(ENABLE_MOTOR01_GPIO_Port, ENABLE_MOTOR01_Pin,
                    GPIO_PIN_SET);
  HAL_GPIO_WritePin(ENABLE_MOTOR02_GPIO_Port, ENABLE_MOTOR02_Pin,
                    GPIO_PIN_SET);

  if (!motor_driver_init(&tmc_motor_01, &tmc_port_motor_01, huart_01->Instance,
                         ENABLE_MOTOR01_GPIO_Port, ENABLE_MOTOR01_Pin)) {
    motor_comm_warning(1u);
  }
  if (!motor_driver_init(&tmc_motor_02, &tmc_port_motor_02, huart_02->Instance,
                         ENABLE_MOTOR02_GPIO_Port, ENABLE_MOTOR02_Pin)) {
    motor_comm_warning(2u);
  }

  motor_stepper_init(&stepper_motor_01, &stepper_hal_motor_01,
                     STEP_MOTOR01_GPIO_Port, STEP_MOTOR01_Pin,
                     DIR_MOTOR01_GPIO_Port, DIR_MOTOR01_Pin);
  motor_stepper_init(&stepper_motor_02, &stepper_hal_motor_02,
                     STEP_MOTOR02_GPIO_Port, STEP_MOTOR02_Pin,
                     DIR_MOTOR02_GPIO_Port, DIR_MOTOR02_Pin);

  // Make sure both drivers follow the STEP/DIR interface (VACTUAL = 0).
  tmc2209_move_using_step_dir_interface(&tmc_motor_01);
  tmc2209_move_using_step_dir_interface(&tmc_motor_02);
}

// Hold STEP high long enough for the TMC2209 (>= 100 ns); at 32 MHz this
// loop is roughly a microsecond.
static void step_pulse_width_delay(void) {
  for (volatile uint32_t i = 0u; i < 8u; ++i) {
  }
}

// Turn both motors one full revolution over APP_ROTATION_TIME_MS, blocking.
// clockwise maps to DIR high; if a motor spins the other way, swap one of
// its coil pairs or use tmc2209_enable_inverse_motor_direction().
static void rotate_one_revolution(bool clockwise) {
  Stepper_SetDirection(&stepper_motor_01, clockwise);
  Stepper_SetDirection(&stepper_motor_02, clockwise);

  // Pace the microsteps against the millisecond tick so the revolution takes
  // APP_ROTATION_TIME_MS regardless of the loop overhead.
  uint32_t const start = HAL_GetTick();
  for (uint32_t step = 1u; step <= APP_MICROSTEPS_PER_REV; ++step) {
    uint32_t const due =
        start + (step * APP_ROTATION_TIME_MS) / APP_MICROSTEPS_PER_REV;
    while ((int32_t)(HAL_GetTick() - due) < 0) {
    }
    Stepper_StepPulseBegin(&stepper_motor_01);
    Stepper_StepPulseBegin(&stepper_motor_02);
    step_pulse_width_delay();
    Stepper_StepPulseEnd(&stepper_motor_01);
    Stepper_StepPulseEnd(&stepper_motor_02);
  }
}

void App_run(void) {
  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
  rotate_one_revolution(true);
}

// void Test_UART(void){
//   const uint8_t data = "Howdy."
//   HAL_UART_Transmit(tmc_port_motor_01.usart, &data, sizeof(data), 200)
// }