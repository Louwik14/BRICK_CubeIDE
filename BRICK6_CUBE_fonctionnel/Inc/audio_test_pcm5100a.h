#ifndef AUDIO_TEST_PCM5100A_H
#define AUDIO_TEST_PCM5100A_H

#include "brick6_refactor.h"

#include <stdint.h>

typedef struct __SAI_HandleTypeDef SAI_HandleTypeDef;

/*
 * Local PCM5100A test mode:
 * - Define PCM5100A_LOCAL_TEST_SINE to generate a standalone sine on SAI2.
 * - Undefine to return to the audio_core-driven path.
 */
#define PCM5100A_LOCAL_TEST_SINE
#define PCM5100A_LOCAL_TEST_FREQ_HZ 440U

enum
{
  AUDIO_TEST_PCM5100A_SAMPLE_RATE = 48000U,
  AUDIO_TEST_PCM5100A_CHANNELS = 2U,
  AUDIO_TEST_PCM5100A_FRAMES_PER_HALF = 256U,
  AUDIO_TEST_PCM5100A_BUFFER_FRAMES = (AUDIO_TEST_PCM5100A_FRAMES_PER_HALF * 2U),
  AUDIO_TEST_PCM5100A_BUFFER_SAMPLES = (AUDIO_TEST_PCM5100A_BUFFER_FRAMES * AUDIO_TEST_PCM5100A_CHANNELS)
};

#ifdef AUDIO_TEST_PCM5100A
void audio_test_pcm5100a_init(SAI_HandleTypeDef *hsai);
void audio_test_pcm5100a_start(void);
void audio_test_pcm5100a_on_tx_half(SAI_HandleTypeDef *hsai);
void audio_test_pcm5100a_on_tx_full(SAI_HandleTypeDef *hsai);
void audio_test_pcm5100a_tasklet_poll(void);
uint32_t AudioTest_PCM5100A_GetTxHalfCount(void);
uint32_t AudioTest_PCM5100A_GetTxFullCount(void);
#else
static inline void audio_test_pcm5100a_init(SAI_HandleTypeDef *hsai)
{
  (void)hsai;
}

static inline void audio_test_pcm5100a_start(void)
{
}

static inline void audio_test_pcm5100a_on_tx_half(SAI_HandleTypeDef *hsai)
{
  (void)hsai;
}

static inline void audio_test_pcm5100a_on_tx_full(SAI_HandleTypeDef *hsai)
{
  (void)hsai;
}

static inline void audio_test_pcm5100a_tasklet_poll(void)
{
}

static inline uint32_t AudioTest_PCM5100A_GetTxHalfCount(void)
{
  return 0U;
}

static inline uint32_t AudioTest_PCM5100A_GetTxFullCount(void)
{
  return 0U;
}
#endif

#endif /* AUDIO_TEST_PCM5100A_H */
