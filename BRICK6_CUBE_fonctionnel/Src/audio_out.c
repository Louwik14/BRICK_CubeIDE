#include "audio_out.h"
#include "audio_in.h"
#include "sai.h"
#include <string.h>

static int32_t audio_out_buffer[AUDIO_OUT_BUFFER_SAMPLES];

static volatile uint32_t audio_out_half_events = 0;
static volatile uint32_t audio_out_full_events = 0;

static SAI_HandleTypeDef *audio_out_sai = NULL;

bool audio_test_loopback_enable = true;

/* Copy one half RX -> TX */
void loopback_copy_half(uint32_t half)
{
  uint32_t offset =
      half * (AUDIO_OUT_FRAMES_PER_HALF * AUDIO_OUT_WORDS_PER_FRAME);

  int32_t *tx = &audio_out_buffer[offset];
  const int32_t *rx = AudioIn_GetHalfBlock(half);

  if (!audio_test_loopback_enable)
  {
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

void AudioOut_Init(SAI_HandleTypeDef *hsai)
{
  audio_out_sai = hsai;
  memset(audio_out_buffer, 0, sizeof(audio_out_buffer));
}

void AudioOut_Start(void)
{
  if (!audio_out_sai)
    return;

  /* DMA stability: circular mode, very high priority; FIFO disabled or
     full with single-burst (no large bursts) to reduce audio jitter. */
  HAL_SAI_Transmit_DMA(audio_out_sai,
                      (uint8_t *)audio_out_buffer,
                      AUDIO_OUT_BUFFER_SAMPLES);
}

void AudioOut_ProcessHalf(void)
{
  audio_out_half_events++;
}

void AudioOut_ProcessFull(void)
{
  audio_out_full_events++;
}

uint32_t AudioOut_GetHalfEvents(void)
{
  return audio_out_half_events;
}

uint32_t AudioOut_GetFullEvents(void)
{
  return audio_out_full_events;
}

/* Dummy legacy poll */
void audio_tasklet_poll(void)
{
  /* Copy outside IRQ to avoid jitter/glitches from long ISR processing. */
  if (AudioIn_IsHalfReady(0U))
  {
    loopback_copy_half(0U);
    AudioIn_ClearHalfReady(0U);
  }

  if (AudioIn_IsHalfReady(1U))
  {
    loopback_copy_half(1U);
    AudioIn_ClearHalfReady(1U);
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
    loopback_copy_half(0);
  }
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    loopback_copy_half(1);
  }
}
