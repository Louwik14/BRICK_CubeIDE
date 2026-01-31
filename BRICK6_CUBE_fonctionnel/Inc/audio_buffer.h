#ifndef AUDIO_BUFFER_H
#define AUDIO_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint32_t base_index;
  uint32_t capacity_samples;
  volatile uint32_t write_pos;
  volatile uint32_t read_pos;
  volatile uint32_t fill_level;
} audio_buffer_t;

bool audio_buffer_init(audio_buffer_t *buffer, uint32_t capacity_samples, uint32_t alignment);
void audio_buffer_reset(audio_buffer_t *buffer);
uint32_t audio_buffer_write(audio_buffer_t *buffer, const int32_t *src, uint32_t samples);
uint32_t audio_buffer_read(audio_buffer_t *buffer, int32_t *dst, uint32_t samples);
uint32_t audio_buffer_peek(const audio_buffer_t *buffer, int32_t *dst, uint32_t samples);
uint32_t audio_buffer_skip(audio_buffer_t *buffer, uint32_t samples);
uint32_t audio_buffer_available(const audio_buffer_t *buffer);
uint32_t audio_buffer_free(const audio_buffer_t *buffer);

#endif /* AUDIO_BUFFER_H */
