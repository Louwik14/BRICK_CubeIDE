#ifndef AUDIO_TEST_PCM5100A_H
#define AUDIO_TEST_PCM5100A_H

#include <stdint.h>

typedef struct __SAI_HandleTypeDef SAI_HandleTypeDef;

enum
{
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
