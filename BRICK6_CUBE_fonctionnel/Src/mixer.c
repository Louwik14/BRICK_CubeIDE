/**
 * @file mixer.c
 * @brief Mixeur int32 avec saturation 24-bit.
 */

#include "mixer.h"

enum
{
  AUDIO_SAMPLE_SHIFT = 8,
  AUDIO_SAMPLE_MAX = 0x7FFFFF00,
  AUDIO_SAMPLE_MIN = (int32_t)0x80000000
};

static int32_t mixer_saturate(int64_t value)
{
  if (value > AUDIO_SAMPLE_MAX)
  {
    return AUDIO_SAMPLE_MAX;
  }
  if (value < AUDIO_SAMPLE_MIN)
  {
    return AUDIO_SAMPLE_MIN;
  }
  return (int32_t)value;
}

void mixer_clear(int32_t *dest, uint32_t samples)
{
  if (dest == NULL)
  {
    return;
  }

  for (uint32_t i = 0; i < samples; ++i)
  {
    dest[i] = 0;
  }
}

void mixer_add(int32_t *dest, const int32_t *src, uint32_t samples, float gain)
{
  if ((dest == NULL) || (src == NULL))
  {
    return;
  }

  for (uint32_t i = 0; i < samples; ++i)
  {
    int64_t mixed = (int64_t)dest[i] + (int64_t)((float)src[i] * gain);
    dest[i] = mixer_saturate(mixed);
  }
}
