/**
 * @file fx_chain.c
 * @brief FX simples (volume + delay SDRAM).
 */

#include "fx_chain.h"
#include "sdram.h"
#include "sdram_alloc.h"

enum
{
  FX_DELAY_CHANNELS = 2U,
  AUDIO_SAMPLE_MAX = 0x7FFFFF00,
  AUDIO_SAMPLE_MIN = (int32_t)0x80000000
};

static uint32_t fx_delay_base_index = 0U;
static uint32_t fx_delay_frames = 0U;
static uint32_t fx_delay_write_pos = 0U;

static int32_t fx_saturate(int64_t value)
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

void fx_chain_init(fx_chain_t *fx)
{
  if (fx == NULL)
  {
    return;
  }

  for (uint32_t ch = 0; ch < 8U; ++ch)
  {
    fx->volume[ch] = 1.0f;
  }
  fx->delay_enabled = false;
  fx->delay_frames = 0U;
  fx->delay_feedback = 0.35f;
  fx->delay_mix = 0.25f;
}

bool fx_chain_init_delay(fx_chain_t *fx, uint32_t delay_frames)
{
  if ((fx == NULL) || (delay_frames == 0U))
  {
    return false;
  }

  uint32_t total_samples = delay_frames * FX_DELAY_CHANNELS;
  void *mem = SDRAM_Alloc(total_samples * sizeof(int32_t), 4U);
  if (mem == NULL)
  {
    fx->delay_enabled = false;
    return false;
  }

  fx_delay_base_index = (uint32_t)(((uintptr_t)mem - SDRAM_BANK_ADDR) / sizeof(uint32_t));
  fx_delay_frames = delay_frames;
  fx_delay_write_pos = 0U;

  for (uint32_t i = 0; i < total_samples; ++i)
  {
    sdram_write32(fx_delay_base_index + i, 0U);
  }

  fx->delay_frames = delay_frames;
  fx->delay_enabled = true;
  return true;
}

void fx_chain_process(fx_chain_t *fx, int32_t *buffer, uint32_t frames, uint32_t channels)
{
  if ((fx == NULL) || (buffer == NULL) || (frames == 0U))
  {
    return;
  }

  for (uint32_t frame = 0; frame < frames; ++frame)
  {
    uint32_t base = frame * channels;
    for (uint32_t ch = 0; ch < channels; ++ch)
    {
      int32_t sample = buffer[base + ch];
      sample = (int32_t)((float)sample * fx->volume[ch]);

      if (fx->delay_enabled && (ch < FX_DELAY_CHANNELS) && (fx_delay_frames > 0U))
      {
        uint32_t delay_index = (fx_delay_write_pos * FX_DELAY_CHANNELS) + ch;
        int32_t delayed = (int32_t)sdram_read32(fx_delay_base_index + delay_index);
        int64_t mixed = (int64_t)sample + (int64_t)((float)delayed * fx->delay_mix);
        int64_t feedback = (int64_t)sample + (int64_t)((float)delayed * fx->delay_feedback);
        buffer[base + ch] = fx_saturate(mixed);
        sdram_write32(fx_delay_base_index + delay_index, (uint32_t)fx_saturate(feedback));
      }
      else
      {
        buffer[base + ch] = sample;
      }
    }

    if (fx->delay_enabled && (fx_delay_frames > 0U))
    {
      fx_delay_write_pos++;
      if (fx_delay_write_pos >= fx_delay_frames)
      {
        fx_delay_write_pos = 0U;
      }
    }
  }
}
