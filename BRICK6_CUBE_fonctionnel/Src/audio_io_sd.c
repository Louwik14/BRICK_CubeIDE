/**
 * @file audio_io_sd.c
 * @brief Bridge SD stream -> ring buffer SDRAM.
 */

#include "audio_io_sd.h"
#include "sd_audio_block_ring.h"

static audio_buffer_t *audio_sd_ring = NULL;

void audio_io_sd_init(audio_buffer_t *ring)
{
  audio_sd_ring = ring;
}

void audio_io_sd_task(void)
{
  if (audio_sd_ring == NULL)
  {
    return;
  }

  uint8_t *read_ptr = audio_block_ring_get_read_ptr(&sd_audio_block_ring);
  if (read_ptr == NULL)
  {
    return;
  }

  uint32_t total_samples = AUDIO_BLOCK_SIZE / sizeof(int16_t);
  uint32_t samples = total_samples;
  int32_t temp[AUDIO_SD_CHANNELS * 256U];

  uint32_t idx = 0U;
  while (samples > 0U)
  {
    uint32_t chunk = samples;
    if (chunk > (sizeof(temp) / sizeof(temp[0])))
    {
      chunk = sizeof(temp) / sizeof(temp[0]);
    }

    for (uint32_t i = 0; i < chunk; ++i)
    {
      int16_t s = ((int16_t *)read_ptr)[idx + i];
      temp[i] = ((int32_t)s) << 8;
    }

    audio_buffer_write(audio_sd_ring, temp, chunk);
    idx += chunk;
    samples -= chunk;
  }

  audio_block_ring_consume(&sd_audio_block_ring);
}

uint32_t audio_io_sd_pop(int32_t *dest, uint32_t frames)
{
  if ((audio_sd_ring == NULL) || (dest == NULL))
  {
    return 0U;
  }

  uint32_t samples = frames * AUDIO_SD_CHANNELS;
  return audio_buffer_read(audio_sd_ring, dest, samples) / AUDIO_SD_CHANNELS;
}
