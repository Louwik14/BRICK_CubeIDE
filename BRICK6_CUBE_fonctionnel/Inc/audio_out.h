#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct __SAI_HandleTypeDef SAI_HandleTypeDef;

enum
{
  AUDIO_OUT_SAMPLE_RATE = 48000U,
  AUDIO_OUT_TDM_SLOTS = 8U,
  AUDIO_OUT_WORDS_PER_FRAME = AUDIO_OUT_TDM_SLOTS,
  AUDIO_OUT_FRAMES_PER_HALF = 256U,
  AUDIO_OUT_BUFFER_FRAMES = (AUDIO_OUT_FRAMES_PER_HALF * 2U),
  AUDIO_OUT_BUFFER_SAMPLES = (AUDIO_OUT_BUFFER_FRAMES * AUDIO_OUT_WORDS_PER_FRAME)
};

extern bool audio_test_sine_enable;
extern bool audio_test_loopback_enable;

void AudioOut_Init(SAI_HandleTypeDef *hsai);
void AudioOut_Start(void);
void AudioOut_ProcessHalf(void);
void AudioOut_ProcessFull(void);
void AudioOut_DebugDump(void);
uint32_t AudioOut_GetHalfEvents(void);
uint32_t AudioOut_GetFullEvents(void);

#endif /* AUDIO_OUT_H */
