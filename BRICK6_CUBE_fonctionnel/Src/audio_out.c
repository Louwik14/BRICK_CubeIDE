#include "audio_out.h"
#include "audio_in.h"
#include "sai.h"
#include <string.h>

static int32_t audio_out_buffer[AUDIO_OUT_BUFFER_SAMPLES];

static volatile uint32_t audio_out_half_events = 0;
static volatile uint32_t audio_out_full_events = 0;
static volatile uint8_t tx_half_free[2] = {0U, 0U};
static volatile uint32_t audio_tx_underrun_fill_zeros = 0;

static SAI_HandleTypeDef *audio_out_sai = NULL;

bool audio_test_loopback_enable = true;

static void AudioOut_FillHalf(uint32_t half)
{
  uint32_t offset =
      half * (AUDIO_OUT_FRAMES_PER_HALF * AUDIO_OUT_WORDS_PER_FRAME);

  int32_t *tx = &audio_out_buffer[offset];
  const int32_t *rx = AudioIn_GetLatestBlock();

  if (!audio_test_loopback_enable || rx == NULL)
  {
    if (rx == NULL)
    {
      audio_tx_underrun_fill_zeros++;
    }

    memset(tx, 0,
           AUDIO_OUT_FRAMES_PER_HALF *
               AUDIO_OUT_WORDS_PER_FRAME *
               sizeof(int32_t));
    return;
  }

  for (uint32_t frame = 0; frame < AUDIO_OUT_FRAMES_PER_HALF; frame++)
  {
    uint32_t idx = frame * AUDIO_OUT_WORDS_PER_FRAME;

    for (uint32_t slot = 0; slot < AUDIO_IN_ACTIVE_SLOTS; slot++)
    {
      tx[idx + slot] = rx[idx + slot] & 0xFFFFFF00;
    }

    for (uint32_t slot = AUDIO_IN_ACTIVE_SLOTS; slot < AUDIO_OUT_TDM_SLOTS; slot++)
    {
      tx[idx + slot] = 0;
    }
  }
}

/* Copy one half RX -> TX (legacy compatibility wrapper) */
void loopback_copy_half(uint32_t half)
{
  AudioOut_FillHalf(half);
}

void AudioOut_Init(SAI_HandleTypeDef *hsai)
{
  audio_out_sai = hsai;
  audio_out_half_events = 0;
  audio_out_full_events = 0;
  tx_half_free[0] = 0U;
  tx_half_free[1] = 0U;
  audio_tx_underrun_fill_zeros = 0;
  memset(audio_out_buffer, 0, sizeof(audio_out_buffer));
}

void AudioOut_Start(void)
{
  if (!audio_out_sai)
    return;

  AudioOut_FillHalf(0U);
  AudioOut_FillHalf(1U);

  /* DMA stability: circular mode, very high priority; FIFO disabled or
     full with single-burst (no large bursts) to reduce audio jitter. */
  HAL_SAI_Transmit_DMA(audio_out_sai,
                      (uint8_t *)audio_out_buffer,
                      AUDIO_OUT_BUFFER_SAMPLES);
}

void AudioOut_ProcessHalf(void)
{
  audio_out_half_events++;
  tx_half_free[0] = 1U;
}

void AudioOut_ProcessFull(void)
{
  audio_out_full_events++;
  tx_half_free[1] = 1U;
}

uint32_t AudioOut_GetHalfEvents(void)
{
  return audio_out_half_events;
}

uint32_t AudioOut_GetFullEvents(void)
{
  return audio_out_full_events;
}

uint32_t AudioOut_GetUnderrunFillZeros(void)
{
  return audio_tx_underrun_fill_zeros;
}

bool AudioOut_IsHalfFree(uint32_t half)
{
  if (half > 1U)
  {
    return false;
  }

  return tx_half_free[half] != 0U;
}

void AudioOut_ClearHalfFree(uint32_t half)
{
  if (half > 1U)
  {
    return;
  }

  tx_half_free[half] = 0U;
}

/* Dummy legacy poll */
void audio_tasklet_poll(void)
{
  if (AudioOut_IsHalfFree(0U))
  {
    AudioOut_FillHalf(0U);
    AudioOut_ClearHalfFree(0U);
  }

  if (AudioOut_IsHalfFree(1U))
  {
    AudioOut_FillHalf(1U);
    AudioOut_ClearHalfFree(1U);
  }
}

int32_t *AudioOut_GetHalfBlock(uint32_t half)
{
  uint32_t offset =
      half * (AUDIO_OUT_FRAMES_PER_HALF * AUDIO_OUT_WORDS_PER_FRAME);

  return &audio_out_buffer[offset];
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    AudioOut_ProcessHalf();
  }
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    AudioOut_ProcessFull();
  }
}
