/**
 * @file routing.c
 * @brief Mapping simple des canaux (identity par défaut).
 */

#include "routing.h"

static const int8_t routing_map_default[8] = {0, 1, 2, 3, 4, 5, 6, 7};

void routing_apply(const int32_t *src, int32_t *dst, uint32_t frames, uint32_t channels)
{
  if ((src == NULL) || (dst == NULL) || (channels == 0U))
  {
    return;
  }

  for (uint32_t frame = 0; frame < frames; ++frame)
  {
    uint32_t base = frame * channels;
    for (uint32_t ch = 0; ch < channels; ++ch)
    {
      int8_t map = routing_map_default[ch];
      if ((map < 0) || ((uint32_t)map >= channels))
      {
        dst[base + ch] = 0;
      }
      else
      {
        dst[base + ch] = src[base + (uint32_t)map];
      }
    }
  }
}
