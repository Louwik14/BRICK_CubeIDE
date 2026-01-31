#ifndef AUDIO_CORE_H
#define AUDIO_CORE_H

#include <stdint.h>

typedef struct __SAI_HandleTypeDef SAI_HandleTypeDef;

enum
{
  AUDIO_CORE_SAMPLE_RATE = 48000U,
  AUDIO_CORE_CHANNELS = 8U,
  AUDIO_CORE_FRAMES_PER_BLOCK = 256U,
  AUDIO_CORE_SAMPLES_PER_BLOCK = AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS
};

void audio_core_init(SAI_HandleTypeDef *tx_sai, SAI_HandleTypeDef *rx_sai);
void audio_core_start(void);
void audio_tasklet_poll(void);

#endif /* AUDIO_CORE_H */
