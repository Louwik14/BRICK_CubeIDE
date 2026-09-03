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
#define OLED_DC_Pin GPIO_PIN_13
#define OLED_DC_GPIO_Port GPIOC
#define OLED_RES_Pin GPIO_PIN_14
#define OLED_RES_GPIO_Port GPIOC
#define OLED_CS_Pin GPIO_PIN_15
#define OLED_CS_GPIO_Port GPIOC
#define MUX_HALL_ANAL01_Pin GPIO_PIN_1
#define MUX_HALL_ANAL01_GPIO_Port GPIOC
#define SR_DATA_Pin GPIO_PIN_2
#define SR_DATA_GPIO_Port GPIOA
#define TIM_DATALED_Pin GPIO_PIN_3
#define TIM_DATALED_GPIO_Port GPIOA
#define MUX_HALL_ANAL02_Pin GPIO_PIN_4
#define MUX_HALL_ANAL02_GPIO_Port GPIOA
#define MUX_HALL_ANAL03_Pin GPIO_PIN_5
#define MUX_HALL_ANAL03_GPIO_Port GPIOA
#define SYNC_OUT_Pin GPIO_PIN_6
#define SYNC_OUT_GPIO_Port GPIOA
#define FUSB302_INT_N_Pin GPIO_PIN_4
#define FUSB302_INT_N_GPIO_Port GPIOC
#define FUSB302_INT_N_EXTI_IRQn EXTI4_IRQn
#define POT_VOLUME_Pin GPIO_PIN_1
#define POT_VOLUME_GPIO_Port GPIOB
#define HOST_FLAG_Pin GPIO_PIN_6
#define HOST_FLAG_GPIO_Port GPIOC
#define HOST_EN_Pin GPIO_PIN_7
#define HOST_EN_GPIO_Port GPIOC
#define MUX_HALL_S2_Pin GPIO_PIN_8
#define MUX_HALL_S2_GPIO_Port GPIOA
#define MUX_HALL_S1_Pin GPIO_PIN_9
#define MUX_HALL_S1_GPIO_Port GPIOA
#define MUX_HALL_S0_Pin GPIO_PIN_10
#define MUX_HALL_S0_GPIO_Port GPIOA
#define SCK_SR_Pin GPIO_PIN_15
#define SCK_SR_GPIO_Port GPIOA
#define POWER_HOLD_Pin GPIO_PIN_4
#define POWER_HOLD_GPIO_Port GPIOD
#define CS_SR_Pin GPIO_PIN_5
#define CS_SR_GPIO_Port GPIOD
#define ENC4_B_Pin GPIO_PIN_6
#define ENC4_B_GPIO_Port GPIOD
#define ENC4_A_Pin GPIO_PIN_7
#define ENC4_A_GPIO_Port GPIOD
#define ENC3_B_Pin GPIO_PIN_9
#define ENC3_B_GPIO_Port GPIOG
#define ENC3_A_Pin GPIO_PIN_10
#define ENC3_A_GPIO_Port GPIOG
#define ENC2_B_Pin GPIO_PIN_11
#define ENC2_B_GPIO_Port GPIOG
#define ENC2_A_Pin GPIO_PIN_12
#define ENC2_A_GPIO_Port GPIOG
#define ENC1_B_Pin GPIO_PIN_13
#define ENC1_B_GPIO_Port GPIOG
#define ENC1_A_Pin GPIO_PIN_14
#define ENC1_A_GPIO_Port GPIOG
#define POWER_BUTTON_SENSE_Pin GPIO_PIN_5
#define POWER_BUTTON_SENSE_GPIO_Port GPIOB
#define BOOTLOADER_TRIGGER_Pin GPIO_PIN_8
#define BOOTLOADER_TRIGGER_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
