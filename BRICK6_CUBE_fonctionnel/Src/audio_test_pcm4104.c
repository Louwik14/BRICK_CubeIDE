/**
 * @file audio_test_pcm4104.c
 * @brief Chemin de test PCM4104 (SAI2 TDM8, 24-bit sur frame 32-bit).
 *
 * Chemin temporaire, activable via AUDIO_TEST_PCM4104.
 * - Utilise audio_core pour produire des blocs 256 frames.
 * - Mappe les canaux 0..3 vers les slots TDM 0..3.
 * - Les slots 4..7 sont forcés à 0 (réservés / inutilisés).
 * - Déclenché par DMA SAI2 Tx half/full complete.
 */

#include "audio_test_pcm4104.h"

#ifdef AUDIO_TEST_PCM4104

#include "audio_core.h"
#include "sai.h"

#include <string.h>

static int32_t pcm4104_tx_buffer[AUDIO_TEST_PCM4104_BUFFER_SAMPLES];
static volatile uint32_t pcm4104_tx_half_count = 0U;
static volatile uint32_t pcm4104_tx_full_count = 0U;
static SAI_HandleTypeDef *pcm4104_sai = NULL;
#if defined(PCM4104_LOCAL_TEST_SINE)
static uint32_t pcm4104_phase = 0U;
static uint32_t pcm4104_phase_inc = 0U;
static const int16_t pcm4104_sine_table[256] = {
  0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512,
  10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
  18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731,
  24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510,
  28898, 29268, 29621, 29956, 30273, 30571, 30852, 31113, 31356, 31580,
  31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
  32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285, 32137, 31971,
  31785, 31580, 31356, 31113, 30852, 30571, 30273, 29956, 29621, 29268,
  28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811,
  24279, 23731, 23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868,
  18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279, 12539, 11793,
  11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808, 4011, 3212, 2410,
  1608, 804, 0, -804, -1608, -2410, -3212, -4011, -4808, -5602, -6393, -7179,
  -7962, -8739, -9512, -10278, -11039, -11793, -12539, -13279, -14010,
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
#else
static volatile uint8_t pcm4104_half_ready = 0U;
static volatile uint8_t pcm4104_full_ready = 0U;
static int32_t pcm4104_core_block[AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS];
#endif

static void pcm4104_fill_half(uint32_t frame_offset)
{
#if defined(PCM4104_LOCAL_TEST_SINE)
  for (uint32_t frame = 0U; frame < AUDIO_TEST_PCM4104_FRAMES_PER_HALF; ++frame)
  {
    uint32_t table_index = (pcm4104_phase >> 16) & 0xFFU;
    int32_t sample24 = ((int32_t)pcm4104_sine_table[table_index]) << 8;
    uint32_t tx_index = (frame_offset + frame) * AUDIO_TEST_PCM4104_TDM_SLOTS;

    for (uint32_t slot = 0U; slot < AUDIO_TEST_PCM4104_DAC_SLOTS; ++slot)
    {
      pcm4104_tx_buffer[tx_index + slot] = sample24;
    }

    for (uint32_t slot = AUDIO_TEST_PCM4104_DAC_SLOTS; slot < AUDIO_TEST_PCM4104_TDM_SLOTS;
         ++slot)
    {
      pcm4104_tx_buffer[tx_index + slot] = 0;
    }

    pcm4104_phase += pcm4104_phase_inc;
  }
#else
  audio_core_process_block(pcm4104_core_block, AUDIO_TEST_PCM4104_FRAMES_PER_HALF);

  for (uint32_t frame = 0U; frame < AUDIO_TEST_PCM4104_FRAMES_PER_HALF; ++frame)
  {
    uint32_t core_index = frame * AUDIO_CORE_CHANNELS;
    uint32_t tx_index = (frame_offset + frame) * AUDIO_TEST_PCM4104_TDM_SLOTS;

    for (uint32_t slot = 0U; slot < AUDIO_TEST_PCM4104_DAC_SLOTS; ++slot)
    {
      pcm4104_tx_buffer[tx_index + slot] = pcm4104_core_block[core_index + slot];
    }

    for (uint32_t slot = AUDIO_TEST_PCM4104_DAC_SLOTS; slot < AUDIO_TEST_PCM4104_TDM_SLOTS;
         ++slot)
    {
      pcm4104_tx_buffer[tx_index + slot] = 0;
    }
  }
#endif
}

void audio_test_pcm4104_init(SAI_HandleTypeDef *hsai)
{
  pcm4104_sai = hsai;
  pcm4104_tx_half_count = 0U;
  pcm4104_tx_full_count = 0U;
  memset(pcm4104_tx_buffer, 0, sizeof(pcm4104_tx_buffer));
#if defined(PCM4104_LOCAL_TEST_SINE)
  pcm4104_phase = 0U;
  pcm4104_phase_inc =
      (PCM4104_LOCAL_TEST_FREQ_HZ * 256U * 65536U) / AUDIO_TEST_PCM4104_SAMPLE_RATE;
  pcm4104_fill_half(0U);
  pcm4104_fill_half(AUDIO_TEST_PCM4104_FRAMES_PER_HALF);
#else
  pcm4104_half_ready = 0U;
  pcm4104_full_ready = 0U;
  pcm4104_fill_half(0U);
  pcm4104_fill_half(AUDIO_TEST_PCM4104_FRAMES_PER_HALF);
#endif
}

void audio_test_pcm4104_start(void)
{
  if (pcm4104_sai == NULL)
  {
    return;
  }

  (void)HAL_SAI_Transmit_DMA(pcm4104_sai,
                             (uint8_t *)pcm4104_tx_buffer,
                             AUDIO_TEST_PCM4104_BUFFER_SAMPLES);
}

void audio_test_pcm4104_on_tx_half(SAI_HandleTypeDef *hsai)
{
  if ((hsai != NULL) && (hsai->Instance == SAI2_Block_A))
  {
    pcm4104_tx_half_count++;
#if defined(PCM4104_LOCAL_TEST_SINE)
    pcm4104_fill_half(0U);
#else
    pcm4104_half_ready = 1U;
#endif
  }
}

void audio_test_pcm4104_on_tx_full(SAI_HandleTypeDef *hsai)
{
  if ((hsai != NULL) && (hsai->Instance == SAI2_Block_A))
  {
    pcm4104_tx_full_count++;
#if defined(PCM4104_LOCAL_TEST_SINE)
    pcm4104_fill_half(AUDIO_TEST_PCM4104_FRAMES_PER_HALF);
#else
    pcm4104_full_ready = 1U;
#endif
  }
}

void audio_test_pcm4104_tasklet_poll(void)
{
#if defined(PCM4104_LOCAL_TEST_SINE)
  return;
#else
  if (pcm4104_half_ready != 0U)
  {
    pcm4104_half_ready = 0U;
    pcm4104_fill_half(0U);
  }

  if (pcm4104_full_ready != 0U)
  {
    pcm4104_full_ready = 0U;
    pcm4104_fill_half(AUDIO_TEST_PCM4104_FRAMES_PER_HALF);
  }
#endif
}

uint32_t AudioTest_PCM4104_GetTxHalfCount(void)
{
  return pcm4104_tx_half_count;
}

uint32_t AudioTest_PCM4104_GetTxFullCount(void)
{
  return pcm4104_tx_full_count;
}

#endif /* AUDIO_TEST_PCM4104 */
