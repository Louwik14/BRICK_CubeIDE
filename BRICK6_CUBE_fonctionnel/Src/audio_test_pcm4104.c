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
static volatile uint8_t pcm4104_half_ready = 0U;
static volatile uint8_t pcm4104_full_ready = 0U;
static int32_t pcm4104_core_block[AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS];
static SAI_HandleTypeDef *pcm4104_sai = NULL;

static void pcm4104_fill_half(uint32_t frame_offset)
{
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
}

void audio_test_pcm4104_init(SAI_HandleTypeDef *hsai)
{
  pcm4104_sai = hsai;
  pcm4104_tx_half_count = 0U;
  pcm4104_tx_full_count = 0U;
  pcm4104_half_ready = 0U;
  pcm4104_full_ready = 0U;
  memset(pcm4104_tx_buffer, 0, sizeof(pcm4104_tx_buffer));
  pcm4104_fill_half(0U);
  pcm4104_fill_half(AUDIO_TEST_PCM4104_FRAMES_PER_HALF);
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
    pcm4104_half_ready = 1U;
  }
}

void audio_test_pcm4104_on_tx_full(SAI_HandleTypeDef *hsai)
{
  if ((hsai != NULL) && (hsai->Instance == SAI2_Block_A))
  {
    pcm4104_tx_full_count++;
    pcm4104_full_ready = 1U;
  }
}

void audio_test_pcm4104_tasklet_poll(void)
{
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
