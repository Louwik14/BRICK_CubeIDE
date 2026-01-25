#include "usb_audio.h"
#include "stm32h7xx_hal.h"
#include <string.h>

/*
 * MODIFICATIONS À FAIRE DANS CUBEMX
 * - Activer USB OTG FS
 * - Activer USB Device
 * - Activer Audio Class
 * - Configurer 48kHz / 24-bit / Stereo
 * - Activer l’horloge 48 MHz USB (PLL3 ou autre)
 * - Activer les IRQ USB
 */

enum
{
  USB_AUDIO_CHANNELS = 2U,
  USB_AUDIO_FIFO_SAMPLES = (USB_AUDIO_FIFO_FRAMES * USB_AUDIO_CHANNELS)
};

static int32_t usb_audio_fifo[USB_AUDIO_FIFO_SAMPLES];
static volatile uint32_t usb_audio_head = 0;
static volatile uint32_t usb_audio_tail = 0;
static volatile uint32_t usb_audio_count = 0;

static void usb_audio_enter_critical(uint32_t *primask)
{
  *primask = __get_PRIMASK();
  __disable_irq();
}

static void usb_audio_exit_critical(uint32_t primask)
{
  if (primask == 0U)
  {
    __enable_irq();
  }
}

bool USBAudio_PopFrame(int32_t *left, int32_t *right)
{
  if (left == NULL || right == NULL)
  {
    return false;
  }

  uint32_t primask = 0;
  usb_audio_enter_critical(&primask);

  if (usb_audio_count == 0U)
  {
    usb_audio_exit_critical(primask);
    *left = 0;
    *right = 0;
    return false;
  }

  uint32_t index = usb_audio_tail * USB_AUDIO_CHANNELS;
  *left = usb_audio_fifo[index];
  *right = usb_audio_fifo[index + 1U];

  usb_audio_tail = (usb_audio_tail + 1U) % USB_AUDIO_FIFO_FRAMES;
  usb_audio_count--;

  usb_audio_exit_critical(primask);
  return true;
}

void USBAudio_PushFrames(const int32_t *interleaved_stereo, uint32_t frames)
{
  if (interleaved_stereo == NULL || frames == 0U)
  {
    return;
  }

  uint32_t primask = 0;
  usb_audio_enter_critical(&primask);

  for (uint32_t frame = 0; frame < frames; ++frame)
  {
    if (usb_audio_count >= USB_AUDIO_FIFO_FRAMES)
    {
      break;
    }

    uint32_t src_index = frame * USB_AUDIO_CHANNELS;
    uint32_t dst_index = usb_audio_head * USB_AUDIO_CHANNELS;

    usb_audio_fifo[dst_index] = interleaved_stereo[src_index];
    usb_audio_fifo[dst_index + 1U] = interleaved_stereo[src_index + 1U];

    usb_audio_head = (usb_audio_head + 1U) % USB_AUDIO_FIFO_FRAMES;
    usb_audio_count++;
  }

  usb_audio_exit_critical(primask);
}
