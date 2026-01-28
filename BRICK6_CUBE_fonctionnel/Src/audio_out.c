#include "audio_out.h"
#include "audio_in.h"
#include "engine_tasklet.h"
#include "sai.h"
#include "usart.h"
#include "brick6_refactor.h"
#include <stdio.h>
#include <string.h>

enum
{
  AUDIO_OUT_TONE_HZ = 100U,
  AUDIO_OUT_TABLE_SIZE = 256U,
  AUDIO_OUT_DAC_CHANNELS = AUDIO_OUT_ACTIVE_SLOTS
};

/*
 * TDM8 frame layout (256-bit frame, 8 slots x 32-bit words):
 *   Slot 0..7 -> CS42448 DAC channels 1..8 (24-bit left aligned)
 */
static int32_t audio_out_buffer[AUDIO_OUT_BUFFER_SAMPLES];
static volatile uint32_t audio_out_half_events = 0;
static volatile uint32_t audio_out_full_events = 0;
static uint32_t audio_out_phase = 0;
static uint32_t audio_out_phase_inc = 0;
static SAI_HandleTypeDef *audio_out_sai = NULL;

bool audio_test_sine_enable = true;
bool audio_test_loopback_enable = false;
volatile uint8_t audio_dma_half_ready = 0U;
volatile uint8_t audio_dma_full_ready = 0U;

static const int16_t audio_out_sine_table[AUDIO_OUT_TABLE_SIZE] = {
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

static void audio_out_fill_samples(uint32_t frame_offset, uint32_t frame_count)
{
  uint32_t index = frame_offset * AUDIO_OUT_WORDS_PER_FRAME;
  const int32_t *audio_in_block = AudioIn_GetLatestBlock();

  for (uint32_t frame = 0; frame < frame_count; ++frame)
  {
    if (audio_test_sine_enable)
    {
      uint32_t table_index = (audio_out_phase >> 16) & (AUDIO_OUT_TABLE_SIZE - 1U);
      int32_t sample24 = ((int32_t)audio_out_sine_table[table_index]) << 8;

      for (uint32_t slot = 0; slot < AUDIO_OUT_DAC_CHANNELS; ++slot)
      {
        audio_out_buffer[index + slot] = sample24;
      }

      audio_out_phase += audio_out_phase_inc;
    }
    else if (audio_test_loopback_enable && audio_in_block != NULL)
    {
      uint32_t in_index = frame * AUDIO_OUT_WORDS_PER_FRAME;
      uint32_t loop_slots =
          ((uint32_t)AUDIO_IN_ACTIVE_SLOTS < (uint32_t)AUDIO_OUT_DAC_CHANNELS)
              ? (uint32_t)AUDIO_IN_ACTIVE_SLOTS
              : (uint32_t)AUDIO_OUT_DAC_CHANNELS;


      for (uint32_t slot = 0; slot < loop_slots; ++slot)
      {
        audio_out_buffer[index + slot] = audio_in_block[in_index + slot];
      }

      for (uint32_t slot = loop_slots; slot < AUDIO_OUT_TDM_SLOTS; ++slot)
      {
        audio_out_buffer[index + slot] = 0;
      }
    }
    else
    {
      for (uint32_t slot = 0; slot < AUDIO_OUT_TDM_SLOTS; ++slot)
      {
        audio_out_buffer[index + slot] = 0;
      }
    }

    index += AUDIO_OUT_WORDS_PER_FRAME;
  }
}

void AudioOut_DebugDump(void)
{
  char buf[160];

  const char *header = "\r\n--- TDM8 TX DUMP ---\r\n";

  HAL_UART_Transmit(&huart1, (uint8_t *)header, (uint16_t)strlen(header), 100);

  for (int frame = 0; frame < 8; frame++)
  {
    int idx = frame * (int)AUDIO_OUT_WORDS_PER_FRAME;
    snprintf(buf, sizeof(buf),
             "F%02d: %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX\r\n",
             frame,
             (unsigned long)audio_out_buffer[idx + 0],
             (unsigned long)audio_out_buffer[idx + 1],
             (unsigned long)audio_out_buffer[idx + 2],
             (unsigned long)audio_out_buffer[idx + 3],
             (unsigned long)audio_out_buffer[idx + 4],
             (unsigned long)audio_out_buffer[idx + 5],
             (unsigned long)audio_out_buffer[idx + 6],
             (unsigned long)audio_out_buffer[idx + 7]);
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, strlen(buf), 100);
  }
}

void AudioOut_Init(SAI_HandleTypeDef *hsai)
{
  audio_out_sai = hsai;
  audio_out_phase = 0;
  audio_out_phase_inc = (AUDIO_OUT_TONE_HZ * AUDIO_OUT_TABLE_SIZE * 65536U) / AUDIO_OUT_SAMPLE_RATE;
  audio_out_half_events = 0;
  audio_out_full_events = 0;
  audio_dma_half_ready = 0U;
  audio_dma_full_ready = 0U;

  audio_out_fill_samples(0U, AUDIO_OUT_BUFFER_FRAMES);
}

void AudioOut_Start(void)
{
  if (audio_out_sai == NULL)
  {
    return;
  }

  (void)HAL_SAI_Transmit_DMA(audio_out_sai, (uint8_t *)audio_out_buffer, AUDIO_OUT_BUFFER_SAMPLES);
}

void AudioOut_ProcessHalf(void)
{
#if BRICK6_REFACTOR_STEP_2
  audio_dma_half_ready = 1U;
  audio_out_half_events++;
#else
  audio_out_fill_samples(0U, AUDIO_OUT_FRAMES_PER_HALF);
  audio_out_half_events++;
#endif
}

void AudioOut_ProcessFull(void)
{
#if BRICK6_REFACTOR_STEP_2
  audio_dma_full_ready = 1U;
  audio_out_full_events++;
#else
  audio_out_fill_samples(AUDIO_OUT_FRAMES_PER_HALF, AUDIO_OUT_FRAMES_PER_HALF);
  audio_out_full_events++;
#endif
}

void audio_tasklet_poll(void)
{
#if BRICK6_REFACTOR_STEP_2
  if (audio_dma_half_ready != 0U)
  {
    audio_dma_half_ready = 0U;
    /* TODO: STM32H7 DCache/MPU enabled -> add cache maintenance for audio_out_buffer. */
    audio_out_fill_samples(0U, AUDIO_OUT_FRAMES_PER_HALF);
#if BRICK6_REFACTOR_STEP_3
    engine_tasklet_notify_frames(AUDIO_OUT_FRAMES_PER_HALF);
#endif
  }

  if (audio_dma_full_ready != 0U)
  {
    audio_dma_full_ready = 0U;
    /* TODO: STM32H7 DCache/MPU enabled -> add cache maintenance for audio_out_buffer. */
    audio_out_fill_samples(AUDIO_OUT_FRAMES_PER_HALF, AUDIO_OUT_FRAMES_PER_HALF);
#if BRICK6_REFACTOR_STEP_3
    engine_tasklet_notify_frames(AUDIO_OUT_FRAMES_PER_HALF);
#endif
  }
#else
  (void)audio_dma_half_ready;
  (void)audio_dma_full_ready;
#endif
}

uint32_t AudioOut_GetHalfEvents(void)
{
  return audio_out_half_events;
}

uint32_t AudioOut_GetFullEvents(void)
{
  return audio_out_full_events;
}
