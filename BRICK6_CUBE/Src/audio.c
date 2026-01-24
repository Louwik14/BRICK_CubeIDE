#include "audio.h"

#include "dma.h"
#include "sai.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

enum
{
  AUDIO_SAMPLE_RATE = 44100U,
  AUDIO_TONE_HZ = 1000U,
  AUDIO_TABLE_SIZE = 256U,
  AUDIO_CHANNELS = 2U,
  AUDIO_BYTES_PER_SAMPLE = 3U,
  AUDIO_FRAMES_PER_HALF = 256U,
  AUDIO_BUFFER_FRAMES = (AUDIO_FRAMES_PER_HALF * 2U),
  AUDIO_BUFFER_SAMPLES = (AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS),
  AUDIO_BUFFER_BYTES = (AUDIO_BUFFER_SAMPLES * AUDIO_BYTES_PER_SAMPLE)
};

/* 24-bit native stream: L0[3] R0[3] L1[3] R1[3] ... */
static uint8_t audio_buffer[AUDIO_BUFFER_BYTES];
static volatile uint32_t audio_half_events = 0;
static volatile uint32_t audio_full_events = 0;
static uint32_t audio_phase = 0;
static uint32_t audio_phase_inc = 0;

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

static void uart_log(const char *message)
{
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)message, (uint16_t)strlen(message), 10);
}

static HAL_StatusTypeDef audio_configure_dma_24bit(void)
{
  HAL_StatusTypeDef status;

  /* Reconfigure DMA for 1-byte transfers so the wire sees exactly 24 bits/slot. */
  status = HAL_DMA_DeInit(&hdma_sai1_a);
  if (status != HAL_OK)
  {
    return status;
  }

  hdma_sai1_a.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_sai1_a.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  hdma_sai1_a.Init.MemBurst = DMA_MBURST_SINGLE;
  hdma_sai1_a.Init.PeriphBurst = DMA_PBURST_SINGLE;

  status = HAL_DMA_Init(&hdma_sai1_a);
  if (status != HAL_OK)
  {
    return status;
  }

  __HAL_LINKDMA(&hsai_BlockA1, hdmatx, hdma_sai1_a);
  return HAL_OK;
}

static int32_t audio_clamp_24bit(int32_t sample)
{
  if (sample > 0x7FFFFF)
  {
    return 0x7FFFFF;
  }
  if (sample < -0x800000)
  {
    return -0x800000;
  }
  return sample;
}

static void audio_write_sample_24(uint8_t *dst, int32_t sample)
{
  int32_t clamped = audio_clamp_24bit(sample);
  uint32_t packed = (uint32_t)clamped & 0x00FFFFFFU;

  /* Store MSB-first for 24-bit I2S/SAI data slots. */
  dst[0] = (uint8_t)((packed >> 16) & 0xFFU);
  dst[1] = (uint8_t)((packed >> 8) & 0xFFU);
  dst[2] = (uint8_t)(packed & 0xFFU);
}

static void audio_fill_samples(uint32_t byte_offset, uint32_t frame_count)
{
  uint32_t byte_index = byte_offset;

  for (uint32_t frame = 0; frame < frame_count; ++frame)
  {
    uint32_t table_index = (audio_phase >> 16) & (AUDIO_TABLE_SIZE - 1U);
    int32_t sample = (int32_t)audio_sine_table[table_index] * 256;

    audio_write_sample_24(&audio_buffer[byte_index], sample);
    byte_index += AUDIO_BYTES_PER_SAMPLE;
    audio_write_sample_24(&audio_buffer[byte_index], sample);
    byte_index += AUDIO_BYTES_PER_SAMPLE;

    audio_phase += audio_phase_inc;
  }
}

bool audio_init(void)
{
  audio_phase = 0;
  audio_phase_inc = (AUDIO_TONE_HZ * AUDIO_TABLE_SIZE * 65536U) / AUDIO_SAMPLE_RATE;
  audio_fill_samples(0U, AUDIO_BUFFER_FRAMES);

  uart_log("audio init\r\n");
  return true;
}

bool audio_start(void)
{
  HAL_StatusTypeDef dma_status = HAL_OK;
  HAL_StatusTypeDef sai_status = HAL_OK;
  char log_buffer[128];

  uart_log("audio start\r\n");

  dma_status = audio_configure_dma_24bit();
  if (dma_status != HAL_OK)
  {
    snprintf(log_buffer, sizeof(log_buffer),
             "DMA 24-bit config failed: %lu\r\n", (unsigned long)dma_status);
    uart_log(log_buffer);
    return false;
  }

  sai_status = HAL_SAI_Transmit_DMA(&hsai_BlockA1, audio_buffer, AUDIO_BUFFER_BYTES);
  if (sai_status != HAL_OK)
  {
    snprintf(log_buffer, sizeof(log_buffer),
             "HAL_SAI_Transmit_DMA failed: %lu\r\n", (unsigned long)sai_status);
    uart_log(log_buffer);
    return false;
  }

  uart_log("DMA started\r\n");
  return true;
}

void audio_get_stats(uint32_t *half, uint32_t *full)
{
  if (half != NULL)
  {
    *half = audio_half_events;
  }
  if (full != NULL)
  {
    *full = audio_full_events;
  }
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    audio_fill_samples(0U, AUDIO_FRAMES_PER_HALF);
    audio_half_events++;
  }
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    audio_fill_samples(AUDIO_FRAMES_PER_HALF * AUDIO_CHANNELS * AUDIO_BYTES_PER_SAMPLE,
                       AUDIO_FRAMES_PER_HALF);
    audio_full_events++;
  }
}
