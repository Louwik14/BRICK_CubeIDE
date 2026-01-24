/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : codec_pcm5100a.c
  * @brief          : Simple PCM5100A control (mute pin only).
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

#include "codec_pcm5100a.h"
#include "main.h"

void PCM5100A_Init(void)
{
  PCM5100A_Mute(true);
}

void PCM5100A_Mute(bool enable)
{
  GPIO_PinState state = enable ? GPIO_PIN_SET : GPIO_PIN_RESET;

  HAL_GPIO_WritePin(PCM5100A_MUTE_GPIO_Port, PCM5100A_MUTE_Pin, state);
}
