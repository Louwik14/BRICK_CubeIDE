/**
  ******************************************************************************
  * @file    USB_Device/Audio_Standalone/Inc/usbd_audio_if.h
  * @author  MCD Application Team
  * @brief   Header for usbd_audio_if.c file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USBD_AUDIO_IF_H
#define __USBD_AUDIO_IF_H

/* Includes ------------------------------------------------------------------*/
#if defined(__has_include)
#if __has_include("usbd_audio.h")
#include "usbd_audio.h"
#else
typedef struct _USBD_AUDIO_ItfTypeDef USBD_AUDIO_ItfTypeDef;
#endif
#else
typedef struct _USBD_AUDIO_ItfTypeDef USBD_AUDIO_ItfTypeDef;
#endif

/* Exported types ------------------------------------------------------------*/
/* Exported constants --------------------------------------------------------*/
extern USBD_AUDIO_ItfTypeDef USBD_AUDIO_fops;

/* Exported macro ------------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */

#endif /* __USBD_AUDIO_IF_H */
