#ifndef AUDIO_BUFFER_H
#define AUDIO_BUFFER_H

#include <stdint.h>

typedef struct {
  int32_t *data;
  uint32_t capacity;
  uint32_t read_pos;
  uint32_t write_pos;
} audio_buffer_t;

void audio_buffer_init(audio_buffer_t *buf, int32_t *mem, uint32_t capacity);
uint32_t audio_buffer_available(const audio_buffer_t *buf);
uint32_t audio_buffer_free(const audio_buffer_t *buf);
void audio_buffer_reset(audio_buffer_t *buf);

#endif /* AUDIO_BUFFER_H */
