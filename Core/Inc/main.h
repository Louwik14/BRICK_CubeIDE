/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define MUX_POT_S0_Pin GPIO_PIN_2
#define MUX_POT_S0_GPIO_Port GPIOH
#define MUX_POT_S1_Pin GPIO_PIN_3
#define MUX_POT_S1_GPIO_Port GPIOH
#define MUX_POT_S2_Pin GPIO_PIN_4
#define MUX_POT_S2_GPIO_Port GPIOH
#define MUX_POT_ANALO_Pin GPIO_PIN_5
#define MUX_POT_ANALO_GPIO_Port GPIOH
#define SPI5_CS_SR_Pin GPIO_PIN_3
#define SPI5_CS_SR_GPIO_Port GPIOA
#define MUX_HAL_S1_Pin GPIO_PIN_4
#define MUX_HAL_S1_GPIO_Port GPIOA
#define MUX_HAL_S0_Pin GPIO_PIN_5
#define MUX_HAL_S0_GPIO_Port GPIOA
#define MUX_HAL_S2_Pin GPIO_PIN_6
#define MUX_HAL_S2_GPIO_Port GPIOA
#define MUX_HAL_ANALO2_Pin GPIO_PIN_7
#define MUX_HAL_ANALO2_GPIO_Port GPIOA
#define MUX_HAL_ANALO_Pin GPIO_PIN_4
#define MUX_HAL_ANALO_GPIO_Port GPIOC
#define ENC1_A_Pin GPIO_PIN_0
#define ENC1_A_GPIO_Port GPIOB
#define ENC1_A_EXTI_IRQn EXTI0_IRQn
#define ENC1_B_Pin GPIO_PIN_1
#define ENC1_B_GPIO_Port GPIOB
#define ENC2_A_Pin GPIO_PIN_10
#define ENC2_A_GPIO_Port GPIOB
#define ENC2_A_EXTI_IRQn EXTI15_10_IRQn
#define ENC2_B_Pin GPIO_PIN_11
#define ENC2_B_GPIO_Port GPIOB
#define ENC3_A_Pin GPIO_PIN_10
#define ENC3_A_GPIO_Port GPIOH
#define ENC3_B_Pin GPIO_PIN_11
#define ENC3_B_GPIO_Port GPIOH
#define ENC3_B_EXTI_IRQn EXTI15_10_IRQn
#define ENC4_A_Pin GPIO_PIN_12
#define ENC4_A_GPIO_Port GPIOD
#define ENC4_A_EXTI_IRQn EXTI15_10_IRQn
#define ENC4_B_Pin GPIO_PIN_13
#define ENC4_B_GPIO_Port GPIOD
#define SPI2_CS_Pin GPIO_PIN_0
#define SPI2_CS_GPIO_Port GPIOI
#define SPI6_CS_Pin GPIO_PIN_6
#define SPI6_CS_GPIO_Port GPIOD
#define SPI1_CS_Pin GPIO_PIN_10
#define SPI1_CS_GPIO_Port GPIOG
#define SPI5_RES_OLED_Pin GPIO_PIN_4
#define SPI5_RES_OLED_GPIO_Port GPIOI
#define SPI5_DC_OLED_Pin GPIO_PIN_5
#define SPI5_DC_OLED_GPIO_Port GPIOI
#define SPI5_CS_OLED_Pin GPIO_PIN_6
#define SPI5_CS_OLED_GPIO_Port GPIOI

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
