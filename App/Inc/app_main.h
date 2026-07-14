#ifndef __APP_MAIN__
#define __APP_MAIN__

#include "stm32f1xx_hal.h"

void App_init(UART_HandleTypeDef *huart_m1, UART_HandleTypeDef *huart_m2);
void App_run(void);

#endif
