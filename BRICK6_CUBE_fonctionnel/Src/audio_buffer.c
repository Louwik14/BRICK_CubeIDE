#include "audio_buffer.h"

void audio_buffer_init(audio_buffer_t *buf, int32_t *mem, uint32_t capacity) {
  buf->data = mem;
  buf->capacity = capacity;
  buf->read_pos = 0U;
  buf->write_pos = 0U;
}

uint32_t audio_buffer_available(const audio_buffer_t *buf) {
  if (buf->write_pos >= buf->read_pos) {
    return buf->write_pos - buf->read_pos;
  }
  return (buf->capacity - buf->read_pos) + buf->write_pos;
}

uint32_t audio_buffer_free(const audio_buffer_t *buf) {
  if (buf->capacity == 0U) {
    return 0U;
  }
  return (buf->capacity - 1U) - audio_buffer_available(buf);
}

void audio_buffer_reset(audio_buffer_t *buf) {
  buf->read_pos = 0U;
  buf->write_pos = 0U;
}
