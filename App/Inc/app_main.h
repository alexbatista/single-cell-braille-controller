#ifndef __APP_MAIN__
#define __APP_MAIN__

#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_tim.h"

void App_init(UART_HandleTypeDef *huart_m1, UART_HandleTypeDef *huart_m2,
              TIM_HandleTypeDef *htim_m1, TIM_HandleTypeDef *htim_m2);
void App_run(void);

#endif
