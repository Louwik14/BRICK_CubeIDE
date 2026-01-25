#include "audio.h"
#include "sai.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

enum
{
  AUDIO_SAMPLE_RATE = 48000U,
  AUDIO_TONE_HZ = 100U,
  AUDIO_TABLE_SIZE = 256U,

  AUDIO_CHANNELS = 4U,
  AUDIO_FRAMES_PER_HALF = 256U,
  AUDIO_BUFFER_FRAMES = (AUDIO_FRAMES_PER_HALF * 2U),
  AUDIO_BUFFER_SAMPLES = (AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS)
};

static int32_t audio_buffer[AUDIO_BUFFER_SAMPLES];
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

static void audio_fill_samples(uint32_t frame_offset, uint32_t frame_count)
{
  uint32_t index = frame_offset * AUDIO_CHANNELS;

  for (uint32_t frame = 0; frame < frame_count; ++frame)
  {
    uint32_t table_index = (audio_phase >> 16) & (AUDIO_TABLE_SIZE - 1U);
    int32_t sample24 = ((int32_t)audio_sine_table[table_index]) << 8;

    audio_buffer[index + 0] = sample24;
    audio_buffer[index + 1] = sample24;
    audio_buffer[index + 2] = sample24;
    audio_buffer[index + 3] = sample24;

    index += AUDIO_CHANNELS;
    audio_phase += audio_phase_inc;
  }
}

void Audio_DebugDump(void)
{
    char buf[128];

    HAL_UART_Transmit(&huart1, (uint8_t*)"\r\n--- TDM DUMP ---\r\n", 18, 100);

    for (int i = 0; i < 8; i++)
    {
        int idx = i * 4;
        snprintf(buf, sizeof(buf),
                 "F%02d: %08lX %08lX %08lX %08lX\r\n",
                 i,
                 (unsigned long)audio_buffer[idx + 0],
                 (unsigned long)audio_buffer[idx + 1],
                 (unsigned long)audio_buffer[idx + 2],
                 (unsigned long)audio_buffer[idx + 3]);
        HAL_UART_Transmit(&huart1, (uint8_t*)buf, strlen(buf), 100);
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
  audio_fill_samples(AUDIO_FRAMES_PER_HALF, AUDIO_FRAMES_PER_HALF);
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
