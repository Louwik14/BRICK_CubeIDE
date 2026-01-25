#ifndef AUDIO_IN_H
#define AUDIO_IN_H

#include <stdint.h>

typedef struct __SAI_HandleTypeDef SAI_HandleTypeDef;

enum
{
  AUDIO_IN_SAMPLE_RATE = 48000U,
  AUDIO_IN_TDM_SLOTS = 8U,
  AUDIO_IN_WORDS_PER_FRAME = AUDIO_IN_TDM_SLOTS,
  AUDIO_IN_FRAMES_PER_HALF = 256U,
  AUDIO_IN_BUFFER_FRAMES = (AUDIO_IN_FRAMES_PER_HALF * 2U),
  AUDIO_IN_BUFFER_SAMPLES = (AUDIO_IN_BUFFER_FRAMES * AUDIO_IN_WORDS_PER_FRAME)
};

void AudioIn_Init(SAI_HandleTypeDef *hsai);
void AudioIn_ProcessHalf(void);
void AudioIn_ProcessFull(void);
void AudioIn_DebugDump(void);
int32_t *AudioIn_GetBuffer(void);
const int32_t *AudioIn_GetLatestBlock(void);
uint32_t AudioIn_GetBufferSamples(void);
uint32_t AudioIn_GetHalfEvents(void);
uint32_t AudioIn_GetFullEvents(void);

#endif /* AUDIO_IN_H */
