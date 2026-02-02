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
void audio_buffer_get_levels(const audio_buffer_t *buf, uint32_t *available, uint32_t *free);
void audio_buffer_reset(audio_buffer_t *buf);
uint32_t audio_buffer_write(audio_buffer_t *buf, const int32_t *data, uint32_t count);
uint32_t audio_buffer_read(audio_buffer_t *buf, int32_t *out, uint32_t count);

#endif /* AUDIO_BUFFER_H */
