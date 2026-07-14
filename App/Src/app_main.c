#include "app_main.h"
#include "main.h"
#include "usart.h"

#include "stepper.h"
#include "stepper_hal.h"

static UART_HandleTypeDef *uart_motor_01;
static UART_HandleTypeDef *uart_motor_02;

static Stepper_Hal_Stm32_t stepper_hal_motor_02;
static Stepper_t stepper_motor_02;

void App_init(UART_HandleTypeDef *huart_01, UART_HandleTypeDef *huart_02) {

  uart_motor_01 = huart_01;
  uart_motor_02 = huart_02;

  Stepper_Hal_t hal;
  Stepper_Hal_Stm32_Init(&stepper_hal_motor_02, &hal, STEP_MOTOR02_GPIO_Port,
                         STEP_MOTOR02_Pin, DIR_MOTOR02_GPIO_Port,
                         DIR_MOTOR02_Pin);
  Stepper_Init(&stepper_motor_02, &hal);
}

void App_run(void) {}
