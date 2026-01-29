/**
 * @file audio_in.c
 * @brief Acquisition audio TDM8 via SAI/DMA (ADC du CS42448).
 *
 * Ce module maintient un buffer circulaire DMA d'entrée et expose
 * le dernier demi-buffer complet pour consommation par le moteur/audio_out.
 *
 * Rôle dans le système:
 * - Point d'entrée audio du codec vers le reste du firmware.
 * - Fournit un bloc cohérent pour le monitoring/loopback.
 *
 * Contraintes temps réel:
 * - Critique audio: oui (chemin DMA d'entrée).
 * - IRQ: callbacks SAI mettent à jour les indices/flags uniquement.
 * - Tasklet: non (traitement principal en IRQ minimal).
 * - Borné: oui (constante par demi-buffer).
 *
 * Architecture:
 * - Appelé par: brick6_app_init (init), HAL SAI IRQ.
 * - Appelle: HAL SAI / UART (debug dump hors temps réel).
 * - Consommé par: audio_out, diagnostics_tasklet.
 *
 * Règles:
 * - Pas de malloc.
 * - Pas de blocage en IRQ (les dumps UART sont à faire hors IRQ).
 *
 * @note L’API publique est déclarée dans audio_in.h.
 */

#include "audio_in.h"
#include "brick6_refactor.h"
#include "sai.h"
#include "usart.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum
{
  AUDIO_IN_DUMP_FRAMES = 8U
};

/*
 * TDM8 receive layout (256-bit frame, 8 slots x 32-bit words):
 *   Slots 0..5 -> CS42448 ADC channels 1..6 (3 stereo pairs)
 *   Slots 6..7 -> unused (drive zeros on the codec side)
 * Data is 24-bit left aligned in 32-bit words.
 *
 * The DMA runs in circular mode. We treat the buffer as two halves and expose
 * only the most recent completed half to avoid racing the DMA write pointer.
 */
static int32_t audio_in_buffer[AUDIO_IN_BUFFER_SAMPLES];
static volatile uint32_t audio_in_half_events = 0;
static volatile uint32_t audio_in_full_events = 0;
static volatile uint32_t audio_in_latest_half = 0;
static volatile bool audio_in_has_block = false;
static SAI_HandleTypeDef *audio_in_sai = NULL;

void AudioIn_Init(SAI_HandleTypeDef *hsai)
{
  audio_in_sai = hsai;
  audio_in_half_events = 0;
  audio_in_full_events = 0;
  audio_in_latest_half = 0;
  audio_in_has_block = false;
  memset(audio_in_buffer, 0, sizeof(audio_in_buffer));
}

void AudioIn_ProcessHalf(void)
{
  if (audio_in_sai == NULL)
  {
    return;
  }

  /* Half-transfer complete: DMA moves on to the second half. */
  audio_in_latest_half = 0;
  audio_in_has_block = true;
  audio_in_half_events++;
}

void AudioIn_ProcessFull(void)
{
  if (audio_in_sai == NULL)
  {
    return;
  }

  /* Full-transfer complete: DMA wraps to the first half. */
  audio_in_latest_half = 1;
  audio_in_has_block = true;
  audio_in_full_events++;
}

void AudioIn_DebugDump(void)
{
  char buf[200];

  const char *header = "\r\n--- TDM8 RX DUMP ---\r\n";

  HAL_UART_Transmit(&huart1, (uint8_t *)header, (uint16_t)strlen(header), 100);

  const int32_t *block = AudioIn_GetLatestBlock();

  if (block == NULL)
  {
    const char *no_data = "No completed RX block available.\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t *)no_data, (uint16_t)strlen(no_data), 100);
    return;
  }

  for (uint32_t frame = 0; frame < AUDIO_IN_DUMP_FRAMES; ++frame)
  {
    uint32_t idx = frame * AUDIO_IN_WORDS_PER_FRAME;
    snprintf(buf, sizeof(buf),
             "F%02lu: %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX\r\n",
             (unsigned long)frame,
             (unsigned long)block[idx + 0],
             (unsigned long)block[idx + 1],
             (unsigned long)block[idx + 2],
             (unsigned long)block[idx + 3],
             (unsigned long)block[idx + 4],
             (unsigned long)block[idx + 5],
             (unsigned long)block[idx + 6],
             (unsigned long)block[idx + 7]);
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, strlen(buf), 100);
  }
}

int32_t *AudioIn_GetBuffer(void)
{
  return audio_in_buffer;
}

const int32_t *AudioIn_GetLatestBlock(void)
{
  if (!audio_in_has_block)
  {
    return NULL;
  }

  uint32_t offset = audio_in_latest_half * (AUDIO_IN_FRAMES_PER_HALF * AUDIO_IN_WORDS_PER_FRAME);
  return &audio_in_buffer[offset];
}

uint32_t AudioIn_GetBufferSamples(void)
{
  return AUDIO_IN_BUFFER_SAMPLES;
}

uint32_t AudioIn_GetHalfEvents(void)
{
  return audio_in_half_events;
}

uint32_t AudioIn_GetFullEvents(void)
{
  return audio_in_full_events;
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_B)
  {
#if BRICK6_ENABLE_DIAGNOSTICS
    brick6_audio_rx_half_count++;
#endif
    AudioIn_ProcessHalf();
  }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_B)
  {
#if BRICK6_ENABLE_DIAGNOSTICS
    brick6_audio_rx_full_count++;
#endif
    AudioIn_ProcessFull();
  }
}
