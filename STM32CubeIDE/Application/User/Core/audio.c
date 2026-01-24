#include "audio.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "main.h"
#include "usart.h"

#define AUDIO_SAMPLE_RATE_HZ 48000.0f
#define AUDIO_TONE_HZ 440.0f
#define AUDIO_CHANNELS 2U
#define AUDIO_BUFFER_FRAMES 256U
#define AUDIO_BUFFER_SAMPLES (AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS)

extern SAI_HandleTypeDef hsai_BlockA1;
extern UART_HandleTypeDef husart1;

static int16_t audio_buffer[AUDIO_BUFFER_SAMPLES] __attribute__((aligned(32)));
static float audio_phase = 0.0f;

static void audio_log(const char *message)
{
  if (message == NULL)
  {
    return;
  }
  HAL_UART_Transmit(&husart1, (uint8_t *)message, (uint16_t)strlen(message), HAL_MAX_DELAY);
}

static void audio_clean_dcache(void *addr, size_t size)
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  uintptr_t start = (uintptr_t)addr;
  uintptr_t aligned_start = start & ~(uintptr_t)31;
  size_t aligned_size = size + (size_t)(start - aligned_start);
  aligned_size = (aligned_size + 31U) & ~(size_t)31U;
  SCB_CleanDCache_by_Addr((uint32_t *)aligned_start, (int32_t)aligned_size);
#else
  (void)addr;
  (void)size;
#endif
}

void audio_fill_buffer(int16_t *buffer, size_t frames)
{
  const float phase_increment = 2.0f * (float)M_PI * (AUDIO_TONE_HZ / AUDIO_SAMPLE_RATE_HZ);
  const float amplitude = 30000.0f;

  for (size_t i = 0; i < frames; ++i)
  {
    float sample = sinf(audio_phase) * amplitude;
    int16_t sample_i16 = (int16_t)sample;

    buffer[i * AUDIO_CHANNELS] = sample_i16;
    buffer[i * AUDIO_CHANNELS + 1U] = sample_i16;

    audio_phase += phase_increment;
    if (audio_phase >= 2.0f * (float)M_PI)
    {
      audio_phase -= 2.0f * (float)M_PI;
    }
  }

  audio_clean_dcache(buffer, frames * AUDIO_CHANNELS * sizeof(int16_t));
}

void audio_init(void)
{
  audio_log("audio_init()\r\n");
  audio_fill_buffer(audio_buffer, AUDIO_BUFFER_FRAMES / 2U);
  audio_fill_buffer(&audio_buffer[AUDIO_BUFFER_SAMPLES / 2U], AUDIO_BUFFER_FRAMES / 2U);
  audio_log("Buffer filled\r\n");
}

void audio_start(void)
{
  audio_log("audio_start()\r\n");
  if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audio_buffer, AUDIO_BUFFER_SAMPLES) == HAL_OK)
  {
    audio_log("SAI started\r\n");
    audio_log("DMA started\r\n");
  }
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai != &hsai_BlockA1)
  {
    return;
  }

  audio_fill_buffer(audio_buffer, AUDIO_BUFFER_FRAMES / 2U);
  audio_log("DMA half callback\r\n");
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai != &hsai_BlockA1)
  {
    return;
  }

  audio_fill_buffer(&audio_buffer[AUDIO_BUFFER_SAMPLES / 2U], AUDIO_BUFFER_FRAMES / 2U);
  audio_log("DMA complete callback\r\n");
}
