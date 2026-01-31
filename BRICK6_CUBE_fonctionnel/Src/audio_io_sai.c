/**
 * @file audio_io_sai.c
 * @brief Backend SAI DMA TDM8 (IN/OUT) pour moteur audio.
 */

#include "audio_io_sai.h"
#include "brick6_refactor.h"
#include "sai.h"
#include <string.h>

static int32_t audio_sai_tx_buffer[AUDIO_SAI_BUFFER_SAMPLES]
    __attribute__((section(".ram_d1"), aligned(32)));
static int32_t audio_sai_rx_buffer[AUDIO_SAI_BUFFER_SAMPLES]
    __attribute__((section(".ram_d1"), aligned(32)));

static volatile uint8_t audio_sai_tx_half_ready = 0U;
static volatile uint8_t audio_sai_tx_full_ready = 0U;
static volatile uint8_t audio_sai_rx_half_ready = 0U;
static volatile uint8_t audio_sai_rx_full_ready = 0U;

static volatile uint32_t audio_sai_tx_half_events = 0U;
static volatile uint32_t audio_sai_tx_full_events = 0U;
static volatile uint32_t audio_sai_rx_half_events = 0U;
static volatile uint32_t audio_sai_rx_full_events = 0U;

static SAI_HandleTypeDef *audio_sai_tx = NULL;
static SAI_HandleTypeDef *audio_sai_rx = NULL;

void audio_io_sai_init(SAI_HandleTypeDef *tx_sai, SAI_HandleTypeDef *rx_sai)
{
  audio_sai_tx = tx_sai;
  audio_sai_rx = rx_sai;

  audio_sai_tx_half_ready = 0U;
  audio_sai_tx_full_ready = 0U;
  audio_sai_rx_half_ready = 0U;
  audio_sai_rx_full_ready = 0U;

  audio_sai_tx_half_events = 0U;
  audio_sai_tx_full_events = 0U;
  audio_sai_rx_half_events = 0U;
  audio_sai_rx_full_events = 0U;

  memset(audio_sai_tx_buffer, 0, sizeof(audio_sai_tx_buffer));
  memset(audio_sai_rx_buffer, 0, sizeof(audio_sai_rx_buffer));
}

void audio_io_sai_start(void)
{
  if ((audio_sai_tx == NULL) || (audio_sai_rx == NULL))
  {
    return;
  }

  (void)HAL_SAI_Transmit_DMA(audio_sai_tx,
                             (uint8_t *)audio_sai_tx_buffer,
                             AUDIO_SAI_BUFFER_SAMPLES);
  (void)HAL_SAI_Receive_DMA(audio_sai_rx,
                            (uint8_t *)audio_sai_rx_buffer,
                            AUDIO_SAI_BUFFER_SAMPLES);
}

bool audio_io_sai_take_tx_half(uint32_t *half_index)
{
  if (audio_sai_tx_half_ready != 0U)
  {
    audio_sai_tx_half_ready = 0U;
    if (half_index != NULL)
    {
      *half_index = 0U;
    }
    return true;
  }

  if (audio_sai_tx_full_ready != 0U)
  {
    audio_sai_tx_full_ready = 0U;
    if (half_index != NULL)
    {
      *half_index = 1U;
    }
    return true;
  }

  return false;
}

bool audio_io_sai_take_rx_half(uint32_t *half_index)
{
  if (audio_sai_rx_half_ready != 0U)
  {
    audio_sai_rx_half_ready = 0U;
    if (half_index != NULL)
    {
      *half_index = 0U;
    }
    return true;
  }

  if (audio_sai_rx_full_ready != 0U)
  {
    audio_sai_rx_full_ready = 0U;
    if (half_index != NULL)
    {
      *half_index = 1U;
    }
    return true;
  }

  return false;
}

void audio_io_sai_copy_rx_half(uint32_t half_index, int32_t *dest, uint32_t frames)
{
  if ((dest == NULL) || (frames == 0U))
  {
    return;
  }

  if (frames > AUDIO_SAI_FRAMES_PER_HALF)
  {
    frames = AUDIO_SAI_FRAMES_PER_HALF;
  }

  uint32_t offset = half_index * AUDIO_SAI_SAMPLES_PER_HALF;
  uint32_t samples = frames * AUDIO_SAI_SAMPLES_PER_FRAME;
  memcpy(dest, &audio_sai_rx_buffer[offset], samples * sizeof(int32_t));
}

void audio_io_sai_copy_tx_half(uint32_t half_index, const int32_t *src, uint32_t frames)
{
  if ((src == NULL) || (frames == 0U))
  {
    return;
  }

  if (frames > AUDIO_SAI_FRAMES_PER_HALF)
  {
    frames = AUDIO_SAI_FRAMES_PER_HALF;
  }

  uint32_t offset = half_index * AUDIO_SAI_SAMPLES_PER_HALF;
  uint32_t samples = frames * AUDIO_SAI_SAMPLES_PER_FRAME;
  memcpy(&audio_sai_tx_buffer[offset], src, samples * sizeof(int32_t));
}

uint32_t audio_io_sai_get_tx_half_events(void)
{
  return audio_sai_tx_half_events;
}

uint32_t audio_io_sai_get_tx_full_events(void)
{
  return audio_sai_tx_full_events;
}

uint32_t audio_io_sai_get_rx_half_events(void)
{
  return audio_sai_rx_half_events;
}

uint32_t audio_io_sai_get_rx_full_events(void)
{
  return audio_sai_rx_full_events;
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if ((audio_sai_tx != NULL) && (hsai->Instance == audio_sai_tx->Instance))
  {
#if BRICK6_ENABLE_DIAGNOSTICS
    brick6_audio_tx_half_count++;
#endif
    audio_sai_tx_half_ready = 1U;
    audio_sai_tx_half_events++;
  }
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if ((audio_sai_tx != NULL) && (hsai->Instance == audio_sai_tx->Instance))
  {
#if BRICK6_ENABLE_DIAGNOSTICS
    brick6_audio_tx_full_count++;
#endif
    audio_sai_tx_full_ready = 1U;
    audio_sai_tx_full_events++;
  }
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if ((audio_sai_rx != NULL) && (hsai->Instance == audio_sai_rx->Instance))
  {
#if BRICK6_ENABLE_DIAGNOSTICS
    brick6_audio_rx_half_count++;
#endif
    audio_sai_rx_half_ready = 1U;
    audio_sai_rx_half_events++;
  }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if ((audio_sai_rx != NULL) && (hsai->Instance == audio_sai_rx->Instance))
  {
#if BRICK6_ENABLE_DIAGNOSTICS
    brick6_audio_rx_full_count++;
#endif
    audio_sai_rx_full_ready = 1U;
    audio_sai_rx_full_events++;
  }
}
