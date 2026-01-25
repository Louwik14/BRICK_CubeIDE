/**
  ******************************************************************************
  * @file    USB_Device/Audio_Standalone/Src/usbd_audio_if.c
  * @author  MCD Application Team
  * @brief   USB Device Audio interface file.
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

/* Includes ------------------------------------------------------------------ */
#include "usbd_audio_if.h"
#include "usb_audio.h"
#include "stm32h7xx_hal.h"

#if defined(__has_include)
#if __has_include("usbd_audio.h")
#define USB_AUDIO_STACK_AVAILABLE 1
#endif
#endif

#ifndef USB_AUDIO_STACK_AVAILABLE
#define USB_AUDIO_STACK_AVAILABLE 0
#endif

#if USB_AUDIO_STACK_AVAILABLE

/* Private typedef ----------------------------------------------------------- */
/* Private define ------------------------------------------------------------ */
enum
{
  USB_AUDIO_BYTES_PER_SAMPLE = 3U,
  USB_AUDIO_CHANNELS = 2U,
  USB_AUDIO_BYTES_PER_FRAME = (USB_AUDIO_BYTES_PER_SAMPLE * USB_AUDIO_CHANNELS),
  USB_AUDIO_CONVERT_CHUNK_FRAMES = 48U
};

/* Private macro ------------------------------------------------------------- */
/* Private function prototypes ----------------------------------------------- */
static int8_t Audio_Init(uint32_t AudioFreq, uint32_t Volume, uint32_t options);
static int8_t Audio_DeInit(uint32_t options);
static int8_t Audio_PlaybackCmd(uint8_t *pbuf, uint32_t size, uint8_t cmd);
static int8_t Audio_VolumeCtl(uint8_t vol);
static int8_t Audio_MuteCtl(uint8_t cmd);
static int8_t Audio_PeriodicTC(uint8_t *pbuf, uint32_t size, uint8_t cmd);
static int8_t Audio_GetState(void);

/* Private variables --------------------------------------------------------- */
extern USBD_HandleTypeDef USBD_Device;
USBD_AUDIO_ItfTypeDef USBD_AUDIO_fops = {
  Audio_Init,
  Audio_DeInit,
  Audio_PlaybackCmd,
  Audio_VolumeCtl,
  Audio_MuteCtl,
  Audio_PeriodicTC,
  Audio_GetState,
};

/* Private functions --------------------------------------------------------- */

/**
  * @brief  Initializes the AUDIO media low layer.
  * @param  AudioFreq: Audio frequency used to play the audio stream.
  * @param  Volume: Initial volume level (from 0 (Mute) to 100 (Max))
  * @param  options: Reserved for future use
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_Init(uint32_t AudioFreq, uint32_t Volume, uint32_t options)
{
  UNUSED(AudioFreq);
  UNUSED(Volume);
  UNUSED(options);
  return 0;
}

/**
  * @brief  De-Initializes the AUDIO media low layer.
  * @param  options: Reserved for future use
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_DeInit(uint32_t options)
{
  UNUSED(options);
  return 0;
}

/**
  * @brief  Handles AUDIO command.
  * @param  pbuf: Pointer to buffer of data to be sent
  * @param  size: Number of data to be sent (in bytes)
  * @param  cmd: Command opcode
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_PlaybackCmd(uint8_t *pbuf, uint32_t size, uint8_t cmd)
{
  UNUSED(pbuf);
  UNUSED(size);
  UNUSED(cmd);

  return 0;
}

/**
  * @brief  Controls AUDIO Volume.
  * @param  vol: Volume level (0..100)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_VolumeCtl(uint8_t vol)
{
  UNUSED(vol);
  return 0;
}

/**
  * @brief  Controls AUDIO Mute.
  * @param  cmd: Command opcode
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_MuteCtl(uint8_t cmd)
{
  UNUSED(cmd);
  return 0;
}

static int32_t usb_audio_unpack_24bit(const uint8_t *src)
{
  int32_t value = (int32_t)((uint32_t)src[0]
                            | ((uint32_t)src[1] << 8)
                            | ((uint32_t)src[2] << 16));

  if ((value & 0x00800000) != 0)
  {
    value |= 0xFF000000;
  }

  return value << 8;
}

/**
  * @brief  Audio_PeriodicTC
  * @param  cmd: Command opcode
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_PeriodicTC(uint8_t *pbuf, uint32_t size, uint8_t cmd)
{
  UNUSED(cmd);

  if (pbuf == NULL || size < USB_AUDIO_BYTES_PER_FRAME)
  {
    return 0;
  }

  uint32_t frames = size / USB_AUDIO_BYTES_PER_FRAME;
  const uint8_t *src = pbuf;

  while (frames > 0U)
  {
    uint32_t chunk = frames;
    if (chunk > USB_AUDIO_CONVERT_CHUNK_FRAMES)
    {
      chunk = USB_AUDIO_CONVERT_CHUNK_FRAMES;
    }

    int32_t interleaved[USB_AUDIO_CONVERT_CHUNK_FRAMES * USB_AUDIO_CHANNELS];

    for (uint32_t frame = 0; frame < chunk; ++frame)
    {
      const uint8_t *frame_ptr = &src[frame * USB_AUDIO_BYTES_PER_FRAME];
      int32_t left = usb_audio_unpack_24bit(frame_ptr);
      int32_t right = usb_audio_unpack_24bit(frame_ptr + USB_AUDIO_BYTES_PER_SAMPLE);

      uint32_t dst_index = frame * USB_AUDIO_CHANNELS;
      interleaved[dst_index] = left;
      interleaved[dst_index + 1U] = right;
    }

    USBAudio_PushFrames(interleaved, chunk);

    src += chunk * USB_AUDIO_BYTES_PER_FRAME;
    frames -= chunk;
  }

  return 0;
}

/**
  * @brief  Gets AUDIO State.
  * @param  None
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t Audio_GetState(void)
{
  return 0;
}

/**
  * @brief  Manages the DMA full Transfer complete event.
  * @param  None
  * @retval None
  */
void BSP_AUDIO_OUT_TransferComplete_CallBack(uint32_t Instance)
{
  UNUSED(Instance);
  USBD_AUDIO_Sync(&USBD_Device, AUDIO_OFFSET_FULL);
}

/**
  * @brief  Manages the DMA Half Transfer complete event.
  * @param  None
  * @retval None
  */
void BSP_AUDIO_OUT_HalfTransfer_CallBack(uint32_t Instance)
{
  UNUSED(Instance);
  USBD_AUDIO_Sync(&USBD_Device, AUDIO_OFFSET_HALF);
}

#endif /* USB_AUDIO_STACK_AVAILABLE */
