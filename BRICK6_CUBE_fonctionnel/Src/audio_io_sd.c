#include "audio_io_sd.h"

#include "sd_audio_block_ring.h"

#include <string.h>

void audio_io_sd_init(void)
{
}

bool audio_io_sd_has_block(void)
{
  return audio_block_ring_fill_level(&sd_audio_block_ring) > 0U;
}

uint32_t audio_io_sd_read_block(int32_t *dst, uint32_t max_samples)
{
  if (dst == NULL)
  {
    return 0U;
  }

  uint32_t block_samples = AUDIO_BLOCK_SIZE / sizeof(int32_t);
  if (block_samples == 0U)
  {
    return 0U;
  }

  uint32_t blocks_needed =
      (max_samples + block_samples - 1U) / block_samples;
  uint32_t fill_level = audio_block_ring_fill_level(&sd_audio_block_ring);
  if (fill_level < blocks_needed)
  {
    return 0U;
  }

  uint32_t remaining = max_samples;
  uint32_t copied = 0U;

  while (remaining > 0U)
  {
    uint8_t *read_ptr = audio_block_ring_get_read_ptr(&sd_audio_block_ring);
    if (read_ptr == NULL)
    {
      break;
    }

    uint32_t copy_samples = (remaining < block_samples) ? remaining : block_samples;
    memcpy(&dst[copied], read_ptr, copy_samples * sizeof(int32_t));
    audio_block_ring_consume(&sd_audio_block_ring);
    copied += copy_samples;
    remaining -= copy_samples;
  }

  return copied;
}
