#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct __SAI_HandleTypeDef SAI_HandleTypeDef;

enum
{
  AUDIO_OUT_SAMPLE_RATE = 48000U,

  AUDIO_OUT_TDM_SLOTS = 8U,
  AUDIO_OUT_ACTIVE_SLOTS = 8U,

  AUDIO_OUT_WORDS_PER_FRAME = AUDIO_OUT_TDM_SLOTS,

  AUDIO_OUT_FRAMES_PER_HALF = 256U,
  AUDIO_OUT_BUFFER_FRAMES = (AUDIO_OUT_FRAMES_PER_HALF * 2U),

  AUDIO_OUT_BUFFER_SAMPLES =
      (AUDIO_OUT_BUFFER_FRAMES * AUDIO_OUT_WORDS_PER_FRAME)
};

extern bool audio_test_loopback_enable;

void AudioOut_Init(SAI_HandleTypeDef *hsai);
void AudioOut_Start(void);

void AudioOut_ProcessHalf(void);
void AudioOut_ProcessFull(void);

bool AudioOut_IsHalfFree(uint32_t half);
void AudioOut_ClearHalfFree(uint32_t half);
int32_t *AudioOut_GetHalfBlock(uint32_t half);
void loopback_copy_half(uint32_t half);

/* Legacy API */
uint32_t AudioOut_GetHalfEvents(void);
uint32_t AudioOut_GetFullEvents(void);
uint32_t AudioOut_GetUnderrunFillZeros(void);

/* Keep poll symbol for linker */
void audio_tasklet_poll(void);

#endif /* AUDIO_OUT_H */
