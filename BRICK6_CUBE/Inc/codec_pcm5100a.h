/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : codec_pcm5100a.h
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
#ifndef __CODEC_PCM5100A_H__
#define __CODEC_PCM5100A_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

void PCM5100A_Init(void);
void PCM5100A_Mute(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* __CODEC_PCM5100A_H__ */
