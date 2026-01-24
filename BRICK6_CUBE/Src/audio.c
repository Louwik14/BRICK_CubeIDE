#include "audio.h"

#include "sai.h"

enum
{
  AUDIO_SAMPLE_RATE = 44100U,
  AUDIO_TONE_HZ = 1000U,
  AUDIO_TABLE_SIZE = 256U,
  AUDIO_CHANNELS = 2U,
  AUDIO_FRAMES_PER_HALF = 256U,
  AUDIO_BUFFER_FRAMES = (AUDIO_FRAMES_PER_HALF * 2U),
  AUDIO_BUFFER_SAMPLES = (AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS)
};

static int16_t audio_buffer[AUDIO_BUFFER_SAMPLES];
static volatile uint32_t audio_half_events = 0;
static volatile uint32_t audio_full_events = 0;
static uint32_t audio_phase = 0;
static uint32_t audio_phase_inc = 0;
static SAI_HandleTypeDef *audio_sai = NULL;

static const int16_t audio_sine_table[AUDIO_TABLE_SIZE] = {
  0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278,
  11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204,
  18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811,
  25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621,
  29956, 30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285,
  32412, 32521, 32609, 32678, 32728, 32757, 32767, 32757, 32728, 32678, 32609,
  32521, 32412, 32285, 32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
  30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790, 26319,
  25832, 25329, 24811, 24279, 23731, 23170, 22594, 22005, 21403, 20787, 20159,
  19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279, 12539,
  11793, 11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808, 4011, 3212,
  2410, 1608, 804, 0, -804, -1608, -2410, -3212, -4011, -4808, -5602, -6393,
  -7179, -7962, -8739, -9512, -10278, -11039, -11793, -12539, -13279, -14010,
  -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159,
  -20787, -21403, -22005, -22594, -23170, -23731, -24279, -24811, -25329,
  -25832, -26319, -26790, -27245, -27683, -28105, -28510, -28898, -29268,
  -29621, -29956, -30273, -30571, -30852, -31113, -31356, -31580, -31785,
  -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
  -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137,
  -31971, -31785, -31580, -31356, -31113, -30852, -30571, -30273, -29956,
  -29621, -29268, -28898, -28510, -28105, -27683, -27245, -26790, -26319,
  -25832, -25329, -24811, -24279, -23731, -23170, -22594, -22005, -21403,
  -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446,
  -14732, -14010, -13279, -12539, -11793, -11039, -10278, -9512, -8739,
  -7962, -7179, -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804
};

static void audio_fill_samples(uint32_t sample_offset, uint32_t frame_count)
{
  uint32_t sample_index = sample_offset;

  for (uint32_t frame = 0; frame < frame_count; ++frame)
  {
    uint32_t table_index = (audio_phase >> 16) & (AUDIO_TABLE_SIZE - 1U);
    int16_t sample = audio_sine_table[table_index];

    audio_buffer[sample_index++] = sample;
    audio_buffer[sample_index++] = sample;

    audio_phase += audio_phase_inc;
  }
}

void Audio_Init(SAI_HandleTypeDef *hsai)
{
  audio_sai = hsai;
  audio_phase = 0;
  audio_phase_inc = (AUDIO_TONE_HZ * AUDIO_TABLE_SIZE * 65536U) / AUDIO_SAMPLE_RATE;
  audio_half_events = 0;
  audio_full_events = 0;

  audio_fill_samples(0U, AUDIO_BUFFER_FRAMES);
}

void Audio_Start(void)
{
  if (audio_sai == NULL)
  {
    return;
  }

  (void)HAL_SAI_Transmit_DMA(audio_sai, (uint8_t *)audio_buffer, AUDIO_BUFFER_SAMPLES);
}

void Audio_Process_Half(void)
{
  audio_fill_samples(0U, AUDIO_FRAMES_PER_HALF);
  audio_half_events++;
}

void Audio_Process_Full(void)
{
  audio_fill_samples(AUDIO_FRAMES_PER_HALF * AUDIO_CHANNELS, AUDIO_FRAMES_PER_HALF);
  audio_full_events++;
}

uint32_t Audio_GetHalfEvents(void)
{
  return audio_half_events;
}

uint32_t Audio_GetFullEvents(void)
{
  return audio_full_events;
}
