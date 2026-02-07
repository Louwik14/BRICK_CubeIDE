#include "audio_in.h"
#include "sai.h"
#include <string.h>

static int32_t audio_in_buffer[AUDIO_IN_BUFFER_SAMPLES];

static volatile uint32_t audio_in_half_events = 0;
static volatile uint32_t audio_in_full_events = 0;
static volatile uint8_t rx_half_ready[2] = {0U, 0U};
static volatile const int32_t *g_latest_rx_block = NULL;

static SAI_HandleTypeDef *audio_in_sai = NULL;

void AudioIn_Init(SAI_HandleTypeDef *hsai)
{
  audio_in_sai = hsai;
  audio_in_half_events = 0;
  audio_in_full_events = 0;
  rx_half_ready[0] = 0U;
  rx_half_ready[1] = 0U;
  g_latest_rx_block = NULL;

  memset(audio_in_buffer, 0, sizeof(audio_in_buffer));
}

void AudioIn_Start(void)
{
  if (!audio_in_sai)
    return;

  /* DMA stability: circular mode, very high priority; FIFO disabled or
     full with single-burst (no large bursts) to reduce audio jitter. */
  HAL_SAI_Receive_DMA(audio_in_sai,
                      (uint8_t *)audio_in_buffer,
                      AUDIO_IN_BUFFER_SAMPLES);
}

/* ===== ST-style half blocks ===== */

const int32_t *AudioIn_GetHalfBlock(uint32_t half)
{
  uint32_t offset =
      half * (AUDIO_IN_FRAMES_PER_HALF * AUDIO_IN_WORDS_PER_FRAME);

  return &audio_in_buffer[offset];
}

bool AudioIn_IsHalfReady(uint32_t half)
{
  if (half > 1U)
  {
    return false;
  }

  return rx_half_ready[half] != 0U;
}

void AudioIn_ClearHalfReady(uint32_t half)
{
  if (half > 1U)
  {
    return;
  }

  rx_half_ready[half] = 0U;
}

const int32_t *AudioIn_GetLatestBlock(void)
{
  return g_latest_rx_block;
}

/* ===== Legacy API ===== */

int32_t *AudioIn_GetBuffer(void)
{
  return audio_in_buffer;
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

/* ===== Callbacks ===== */

void AudioIn_ProcessHalf(void)
{
  audio_in_half_events++;
  rx_half_ready[0] = 1U;
  g_latest_rx_block = AudioIn_GetHalfBlock(0U);
}

void AudioIn_ProcessFull(void)
{
  audio_in_full_events++;
  rx_half_ready[1] = 1U;
  g_latest_rx_block = AudioIn_GetHalfBlock(1U);
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_B)
  {
    /* IRQ must stay minimal: flag only, no copies here. */
    AudioIn_ProcessHalf();
  }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_B)
  {
    /* IRQ must stay minimal: flag only, no copies here. */
    AudioIn_ProcessFull();
  }
}
