/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#define MIDI_IN_PORTS_NUM   0x01
#define MIDI_OUT_PORTS_NUM  0x01

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define OLED_CS_Pin GPIO_PIN_2
#define OLED_CS_GPIO_Port GPIOE
#define PDN2_Pin GPIO_PIN_4
#define PDN2_GPIO_Port GPIOE
#define PDN1_Pin GPIO_PIN_11
#define PDN1_GPIO_Port GPIOI
#define OLED_RES_Pin GPIO_PIN_6
#define OLED_RES_GPIO_Port GPIOF
#define MUX_HALL_ANALO1_Pin GPIO_PIN_0
#define MUX_HALL_ANALO1_GPIO_Port GPIOC
#define MUX_HALL_ANALO2_Pin GPIO_PIN_1
#define MUX_HALL_ANALO2_GPIO_Port GPIOC
#define MUX_POT_ANALO_Pin GPIO_PIN_3
#define MUX_POT_ANALO_GPIO_Port GPIOC
#define ENC4_B_Pin GPIO_PIN_1
#define ENC4_B_GPIO_Port GPIOA
#define ENC4_A_Pin GPIO_PIN_2
#define ENC4_A_GPIO_Port GPIOA
#define LED_DATA_Pin GPIO_PIN_3
#define LED_DATA_GPIO_Port GPIOA
#define MUX_POT_S2_Pin GPIO_PIN_4
#define MUX_POT_S2_GPIO_Port GPIOA
#define MUX_HALL_S2_Pin GPIO_PIN_5
#define MUX_HALL_S2_GPIO_Port GPIOA
#define MUX_HALL_S1_Pin GPIO_PIN_6
#define MUX_HALL_S1_GPIO_Port GPIOA
#define MUX_POT_S1_Pin GPIO_PIN_7
#define MUX_POT_S1_GPIO_Port GPIOA
#define MUX_HALL_S0_Pin GPIO_PIN_4
#define MUX_HALL_S0_GPIO_Port GPIOC
#define MUX_POT_S0_Pin GPIO_PIN_0
#define MUX_POT_S0_GPIO_Port GPIOB
#define SR_CS_Pin GPIO_PIN_2
#define SR_CS_GPIO_Port GPIOB
#define LED_DEBUG_Pin GPIO_PIN_7
#define LED_DEBUG_GPIO_Port GPIOH
#define ENC2_A_Pin GPIO_PIN_8
#define ENC2_A_GPIO_Port GPIOH
#define ENC3_A_Pin GPIO_PIN_10
#define ENC3_A_GPIO_Port GPIOH
#define ENC3_B_Pin GPIO_PIN_11
#define ENC3_B_GPIO_Port GPIOH
#define ENC2_B_Pin GPIO_PIN_12
#define ENC2_B_GPIO_Port GPIOH
#define OLED_DC_Pin GPIO_PIN_11
#define OLED_DC_GPIO_Port GPIOD
#define ENC1_B_Pin GPIO_PIN_12
#define ENC1_B_GPIO_Port GPIOD
#define SR_SCK_Pin GPIO_PIN_13
#define SR_SCK_GPIO_Port GPIOD
#define SR_DATA_Pin GPIO_PIN_3
#define SR_DATA_GPIO_Port GPIOG
#define ENC1_A_Pin GPIO_PIN_6
#define ENC1_A_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
