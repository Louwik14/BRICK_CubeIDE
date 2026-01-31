/**
 * @file audio_io_usb.c
 * @brief Backend USB Audio (TinyUSB UAC1) vers ring buffer SDRAM.
 */

#include "audio_io_usb.h"
#include "tusb.h"
#include "stm32h7xx_hal.h"
#include <string.h>

static audio_buffer_t *audio_usb_rx_ring = NULL;
static audio_buffer_t *audio_usb_tx_ring = NULL;
static uint32_t audio_usb_sample_rate = 48000U;
static uint32_t audio_usb_frames_per_ms = 48U;

static int32_t audio_usb_rx_tmp[AUDIO_USB_CHANNELS * 48U];
static int32_t audio_usb_tx_tmp[AUDIO_USB_CHANNELS * 48U];
static int16_t audio_usb_tx_packet[AUDIO_USB_CHANNELS * 48U];

void audio_io_usb_init(audio_buffer_t *rx_ring, audio_buffer_t *tx_ring, uint32_t sample_rate)
{
  audio_usb_rx_ring = rx_ring;
  audio_usb_tx_ring = tx_ring;
  audio_usb_sample_rate = (sample_rate == 0U) ? 48000U : sample_rate;
  audio_usb_frames_per_ms = audio_usb_sample_rate / 1000U;
}

void audio_io_usb_reset(void)
{
  if (audio_usb_rx_ring != NULL)
  {
    audio_buffer_reset(audio_usb_rx_ring);
  }
  if (audio_usb_tx_ring != NULL)
  {
    audio_buffer_reset(audio_usb_tx_ring);
  }
}

bool audio_io_usb_rx_done_isr(uint16_t n_bytes_received)
{
  if (audio_usb_rx_ring == NULL)
  {
    return true;
  }

  uint8_t rx_buffer[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX];
  uint16_t remaining = n_bytes_received;

  while (remaining > 0U)
  {
    uint16_t chunk = tu_min16(remaining, sizeof(rx_buffer));
    uint16_t read_count = tud_audio_read(rx_buffer, chunk);
    if (read_count == 0U)
    {
      break;
    }

    uint32_t samples = read_count / AUDIO_USB_BYTES_PER_SAMPLE;
    if (samples > (sizeof(audio_usb_rx_tmp) / sizeof(audio_usb_rx_tmp[0])))
    {
      samples = sizeof(audio_usb_rx_tmp) / sizeof(audio_usb_rx_tmp[0]);
    }

    for (uint32_t i = 0; i < samples; ++i)
    {
      int16_t s = ((int16_t *)rx_buffer)[i];
      audio_usb_rx_tmp[i] = ((int32_t)s) << 8;
    }

    audio_buffer_write(audio_usb_rx_ring, audio_usb_rx_tmp, samples);
    remaining = (uint16_t)(remaining - read_count);
  }

  return true;
}

uint32_t audio_io_usb_pop_rx(int32_t *dest, uint32_t frames)
{
  if ((audio_usb_rx_ring == NULL) || (dest == NULL))
  {
    return 0U;
  }

  uint32_t samples = frames * AUDIO_USB_CHANNELS;
  return audio_buffer_read(audio_usb_rx_ring, dest, samples) / AUDIO_USB_CHANNELS;
}

uint32_t audio_io_usb_push_tx(const int32_t *src, uint32_t frames)
{
  if ((audio_usb_tx_ring == NULL) || (src == NULL))
  {
    return 0U;
  }

  uint32_t samples = frames * AUDIO_USB_CHANNELS;
  return audio_buffer_write(audio_usb_tx_ring, src, samples) / AUDIO_USB_CHANNELS;
}

void audio_io_usb_task(void)
{
  if (audio_usb_tx_ring == NULL)
  {
    return;
  }

  if (!tud_audio_mounted())
  {
    return;
  }

  static uint32_t last_ms = 0U;
  uint32_t now = HAL_GetTick();
  if (now == last_ms)
  {
    return;
  }
  last_ms = now;

  uint32_t frames = audio_usb_frames_per_ms;
  uint32_t samples = frames * AUDIO_USB_CHANNELS;
  uint32_t got = audio_buffer_read(audio_usb_tx_ring, audio_usb_tx_tmp, samples);

  if (got < samples)
  {
    memset(&audio_usb_tx_tmp[got], 0, (samples - got) * sizeof(int32_t));
  }

  for (uint32_t i = 0; i < samples; ++i)
  {
    int32_t s = audio_usb_tx_tmp[i] >> 8;
    if (s > 32767)
    {
      s = 32767;
    }
    else if (s < -32768)
    {
      s = -32768;
    }
    audio_usb_tx_packet[i] = (int16_t)s;
  }

  tud_audio_write((uint8_t *)audio_usb_tx_packet,
                  samples * AUDIO_USB_BYTES_PER_SAMPLE);
}
