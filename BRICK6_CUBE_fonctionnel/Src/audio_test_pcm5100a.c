/**
 * @file audio_test_pcm5100a.c
 * @brief Chemin de test PCM5100A (SAI2 I2S, 24-bit sur frame 32-bit).
 *
 * Chemin temporaire, activable via AUDIO_TEST_PCM5100A.
 * - Utilise audio_core pour produire des blocs 256 frames.
 * - Convertit en stéréo (2 canaux) pour PCM5100A.
 * - Déclenché par DMA SAI2 Tx half/full complete.
 */

#include "audio_test_pcm5100a.h"

#ifdef AUDIO_TEST_PCM5100A

#include "audio_core.h"
#include "sai.h"

#include <string.h>

static int32_t pcm5100a_tx_buffer[AUDIO_TEST_PCM5100A_BUFFER_SAMPLES];
static int32_t pcm5100a_core_block[AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS];
static volatile uint8_t pcm5100a_half_ready = 0U;
static volatile uint8_t pcm5100a_full_ready = 0U;
static SAI_HandleTypeDef *pcm5100a_sai = NULL;

static void pcm5100a_fill_half(uint32_t frame_offset)
{
  audio_core_process_block(pcm5100a_core_block, AUDIO_TEST_PCM5100A_FRAMES_PER_HALF);

  for (uint32_t frame = 0U; frame < AUDIO_TEST_PCM5100A_FRAMES_PER_HALF; ++frame)
  {
    uint32_t core_index = frame * AUDIO_CORE_CHANNELS;
    uint32_t tx_index = (frame_offset + frame) * AUDIO_TEST_PCM5100A_CHANNELS;

    pcm5100a_tx_buffer[tx_index] = pcm5100a_core_block[core_index];
    pcm5100a_tx_buffer[tx_index + 1U] = pcm5100a_core_block[core_index + 1U];
  }
}

void audio_test_pcm5100a_init(SAI_HandleTypeDef *hsai)
{
  pcm5100a_sai = hsai;
  pcm5100a_half_ready = 0U;
  pcm5100a_full_ready = 0U;
  memset(pcm5100a_tx_buffer, 0, sizeof(pcm5100a_tx_buffer));
}

void audio_test_pcm5100a_start(void)
{
  if (pcm5100a_sai == NULL)
  {
    return;
  }

  (void)HAL_SAI_Transmit_DMA(pcm5100a_sai,
                             (uint8_t *)pcm5100a_tx_buffer,
                             AUDIO_TEST_PCM5100A_BUFFER_SAMPLES);
}

void audio_test_pcm5100a_on_tx_half(SAI_HandleTypeDef *hsai)
{
  if ((hsai != NULL) && (hsai->Instance == SAI2_Block_A))
  {
    pcm5100a_half_ready = 1U;
  }
}

void audio_test_pcm5100a_on_tx_full(SAI_HandleTypeDef *hsai)
{
  if ((hsai != NULL) && (hsai->Instance == SAI2_Block_A))
  {
    pcm5100a_full_ready = 1U;
  }
}

void audio_test_pcm5100a_tasklet_poll(void)
{
  if (pcm5100a_half_ready != 0U)
  {
    pcm5100a_half_ready = 0U;
    pcm5100a_fill_half(0U);
  }

  if (pcm5100a_full_ready != 0U)
  {
    pcm5100a_full_ready = 0U;
    pcm5100a_fill_half(AUDIO_TEST_PCM5100A_FRAMES_PER_HALF);
  }
}

#endif /* AUDIO_TEST_PCM5100A */
