#ifndef AUDIO_CORE_H
#define AUDIO_CORE_H

#include <stdint.h>

#define AUDIO_CORE_SAMPLE_RATE 48000U
#define AUDIO_CORE_FRAMES_PER_BLOCK 256U
#define AUDIO_CORE_CHANNELS 8U

typedef struct {
  int32_t samples[AUDIO_CORE_FRAMES_PER_BLOCK][AUDIO_CORE_CHANNELS];
} audio_block_t;

void audio_core_init(void);
void audio_core_process_block(int32_t *out, uint32_t frames);
void audio_core_on_input_block(const int32_t *data, uint32_t frames);

#endif /* AUDIO_CORE_H */
