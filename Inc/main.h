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
#define OLED_DC_Pin GPIO_PIN_3
#define OLED_DC_GPIO_Port GPIOE
#define OLED_RES_Pin GPIO_PIN_4
#define OLED_RES_GPIO_Port GPIOE
#define OLED_CS_Pin GPIO_PIN_5
#define OLED_CS_GPIO_Port GPIOE
#define POT2_Pin GPIO_PIN_4
#define POT2_GPIO_Port GPIOH
#define POT1_Pin GPIO_PIN_5
#define POT1_GPIO_Port GPIOH
#define MUX_HALL_ANALO1_Pin GPIO_PIN_4
#define MUX_HALL_ANALO1_GPIO_Port GPIOC
#define MUX_HALL_ANALO2_Pin GPIO_PIN_7
#define MUX_HALL_ANALO2_GPIO_Port GPIOA
#define SPILINK_SPI6_CS0_Pin GPIO_PIN_4
#define SPILINK_SPI6_CS0_GPIO_Port GPIOA
#define MUX_HALL_S0_Pin GPIO_PIN_5
#define MUX_HALL_S0_GPIO_Port GPIOA
#define MUX_HALL_S1_Pin GPIO_PIN_6
#define MUX_HALL_S1_GPIO_Port GPIOA
#define LED_DATA_Pin GPIO_PIN_10
#define LED_DATA_GPIO_Port GPIOA
#define POWER_HOLD_Pin GPIO_PIN_9
#define POWER_HOLD_GPIO_Port GPIOA
#define HOST_EN_Pin GPIO_PIN_15
#define HOST_EN_GPIO_Port GPIOA
#define ENC1_A_Pin GPIO_PIN_0
#define ENC1_A_GPIO_Port GPIOB
#define ENC1_B_Pin GPIO_PIN_1
#define ENC1_B_GPIO_Port GPIOB
#define ENC2_A_Pin GPIO_PIN_6
#define ENC2_A_GPIO_Port GPIOH
#define ENC2_B_Pin GPIO_PIN_7
#define ENC2_B_GPIO_Port GPIOH
#define ENC3_A_Pin GPIO_PIN_12
#define ENC3_A_GPIO_Port GPIOB
#define ENC3_B_Pin GPIO_PIN_13
#define ENC3_B_GPIO_Port GPIOB
#define ENC4_A_Pin GPIO_PIN_12
#define ENC4_A_GPIO_Port GPIOD
#define ENC4_B_Pin GPIO_PIN_13
#define ENC4_B_GPIO_Port GPIOD
#define MUX_HALL_S2_Pin GPIO_PIN_5
#define MUX_HALL_S2_GPIO_Port GPIOC
#define SR_CS_Pin GPIO_PIN_3
#define SR_CS_GPIO_Port GPIOD
#define CS_SR_Pin SR_CS_Pin
#define CS_SR_GPIO_Port SR_CS_GPIO_Port
#define SR_SCK_Pin GPIO_PIN_4
#define SR_SCK_GPIO_Port GPIOD
#define SCK_SR_Pin SR_SCK_Pin
#define SCK_SR_GPIO_Port SR_SCK_GPIO_Port
#define SR_DATA_Pin GPIO_PIN_5
#define SR_DATA_GPIO_Port GPIOD
#define SPILINK_SPI1_CS0_Pin GPIO_PIN_10
#define SPILINK_SPI1_CS0_GPIO_Port GPIOG

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
