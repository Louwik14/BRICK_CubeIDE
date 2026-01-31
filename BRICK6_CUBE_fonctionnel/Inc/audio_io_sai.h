#ifndef AUDIO_IO_SAI_H
#define AUDIO_IO_SAI_H

#include <stdbool.h>
#include <stdint.h>

typedef struct __SAI_HandleTypeDef SAI_HandleTypeDef;

enum
{
  AUDIO_SAI_SAMPLE_RATE = 48000U,
  AUDIO_SAI_CHANNELS = 8U,
  AUDIO_SAI_FRAMES_PER_HALF = 256U,
  AUDIO_SAI_BUFFER_FRAMES = AUDIO_SAI_FRAMES_PER_HALF * 2U,
  AUDIO_SAI_SAMPLES_PER_FRAME = AUDIO_SAI_CHANNELS,
  AUDIO_SAI_SAMPLES_PER_HALF = AUDIO_SAI_FRAMES_PER_HALF * AUDIO_SAI_SAMPLES_PER_FRAME,
  AUDIO_SAI_BUFFER_SAMPLES = AUDIO_SAI_BUFFER_FRAMES * AUDIO_SAI_SAMPLES_PER_FRAME
};

void audio_io_sai_init(SAI_HandleTypeDef *tx_sai, SAI_HandleTypeDef *rx_sai);
void audio_io_sai_start(void);
bool audio_io_sai_take_tx_half(uint32_t *half_index);
bool audio_io_sai_take_rx_half(uint32_t *half_index);
void audio_io_sai_copy_rx_half(uint32_t half_index, int32_t *dest, uint32_t frames);
void audio_io_sai_copy_tx_half(uint32_t half_index, const int32_t *src, uint32_t frames);
uint32_t audio_io_sai_get_tx_half_events(void);
uint32_t audio_io_sai_get_tx_full_events(void);
uint32_t audio_io_sai_get_rx_half_events(void);
uint32_t audio_io_sai_get_rx_full_events(void);

#endif /* AUDIO_IO_SAI_H */
