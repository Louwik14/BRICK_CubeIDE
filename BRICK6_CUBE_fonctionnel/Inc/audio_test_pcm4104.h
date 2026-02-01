#ifndef AUDIO_TEST_PCM4104_H
#define AUDIO_TEST_PCM4104_H

#include "brick6_refactor.h"

#include <stdint.h>

typedef struct __SAI_HandleTypeDef SAI_HandleTypeDef;

enum
{
  AUDIO_TEST_PCM4104_SAMPLE_RATE = 48000U,
  AUDIO_TEST_PCM4104_TDM_SLOTS = 8U,
  AUDIO_TEST_PCM4104_DAC_SLOTS = 4U,
  AUDIO_TEST_PCM4104_FRAMES_PER_HALF = 256U,
  AUDIO_TEST_PCM4104_BUFFER_FRAMES = (AUDIO_TEST_PCM4104_FRAMES_PER_HALF * 2U),
  AUDIO_TEST_PCM4104_BUFFER_SAMPLES =
      (AUDIO_TEST_PCM4104_BUFFER_FRAMES * AUDIO_TEST_PCM4104_TDM_SLOTS)
};

#ifdef AUDIO_TEST_PCM4104
void audio_test_pcm4104_init(SAI_HandleTypeDef *hsai);
void audio_test_pcm4104_start(void);
void audio_test_pcm4104_on_tx_half(SAI_HandleTypeDef *hsai);
void audio_test_pcm4104_on_tx_full(SAI_HandleTypeDef *hsai);
void audio_test_pcm4104_tasklet_poll(void);
uint32_t AudioTest_PCM4104_GetTxHalfCount(void);
uint32_t AudioTest_PCM4104_GetTxFullCount(void);
#else
static inline void audio_test_pcm4104_init(SAI_HandleTypeDef *hsai)
{
  (void)hsai;
}

static inline void audio_test_pcm4104_start(void)
{
}

static inline void audio_test_pcm4104_on_tx_half(SAI_HandleTypeDef *hsai)
{
  (void)hsai;
}

static inline void audio_test_pcm4104_on_tx_full(SAI_HandleTypeDef *hsai)
{
  (void)hsai;
}

static inline void audio_test_pcm4104_tasklet_poll(void)
{
}

static inline uint32_t AudioTest_PCM4104_GetTxHalfCount(void)
{
  return 0U;
}

static inline uint32_t AudioTest_PCM4104_GetTxFullCount(void)
{
  return 0U;
}
#endif

#endif /* AUDIO_TEST_PCM4104_H */
