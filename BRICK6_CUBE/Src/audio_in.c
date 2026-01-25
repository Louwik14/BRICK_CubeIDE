#include "audio_in.h"
#include "sai.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

enum
{
  AUDIO_IN_DUMP_FRAMES = 8U
};

/*
 * TDM8 receive layout (256-bit frame, 8 slots x 32-bit words):
 *   Slots 0..3 -> ADAU1979 #1 channels 1..4
 *   Slots 4..7 -> ADAU1979 #2 channels 1..4
 * Data is 24-bit left aligned in 32-bit words.
 */
static int32_t audio_in_buffer[AUDIO_IN_BUFFER_SAMPLES];
static volatile uint32_t audio_in_half_events = 0;
static volatile uint32_t audio_in_full_events = 0;
static SAI_HandleTypeDef *audio_in_sai = NULL;

void AudioIn_Init(SAI_HandleTypeDef *hsai)
{
  audio_in_sai = hsai;
  audio_in_half_events = 0;
  audio_in_full_events = 0;
  memset(audio_in_buffer, 0, sizeof(audio_in_buffer));
}

void AudioIn_ProcessHalf(void)
{
  if (audio_in_sai == NULL)
  {
    return;
  }

  audio_in_half_events++;
}

void AudioIn_ProcessFull(void)
{
  if (audio_in_sai == NULL)
  {
    return;
  }

  audio_in_full_events++;
}

void AudioIn_DebugDump(void)
{
  char buf[200];

  const char *header = "\r\n--- TDM8 RX DUMP ---\r\n";

  HAL_UART_Transmit(&huart1, (uint8_t *)header, (uint16_t)strlen(header), 100);

  for (uint32_t frame = 0; frame < AUDIO_IN_DUMP_FRAMES; ++frame)
  {
    uint32_t idx = frame * AUDIO_IN_WORDS_PER_FRAME;
    snprintf(buf, sizeof(buf),
             "F%02lu: %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX\r\n",
             (unsigned long)frame,
             (unsigned long)audio_in_buffer[idx + 0],
             (unsigned long)audio_in_buffer[idx + 1],
             (unsigned long)audio_in_buffer[idx + 2],
             (unsigned long)audio_in_buffer[idx + 3],
             (unsigned long)audio_in_buffer[idx + 4],
             (unsigned long)audio_in_buffer[idx + 5],
             (unsigned long)audio_in_buffer[idx + 6],
             (unsigned long)audio_in_buffer[idx + 7]);
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, strlen(buf), 100);
  }
}

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
