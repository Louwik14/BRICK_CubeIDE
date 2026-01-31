#include "audio_buffer.h"

#include <stddef.h>

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

uint32_t audio_buffer_write(audio_buffer_t *buf, const int32_t *data, uint32_t count) {
  if ((buf == NULL) || (data == NULL) || (buf->capacity == 0U)) {
    return 0U;
  }

  uint32_t free_count = audio_buffer_free(buf);
  if (count > free_count) {
    count = free_count;
  }

  for (uint32_t i = 0; i < count; ++i) {
    buf->data[buf->write_pos] = data[i];
    buf->write_pos = (buf->write_pos + 1U) % buf->capacity;
  }

  return count;
}

uint32_t audio_buffer_read(audio_buffer_t *buf, int32_t *out, uint32_t count) {
  if ((buf == NULL) || (out == NULL) || (buf->capacity == 0U)) {
    return 0U;
  }

  uint32_t available = audio_buffer_available(buf);
  if (count > available) {
    count = available;
  }

  for (uint32_t i = 0; i < count; ++i) {
    out[i] = buf->data[buf->read_pos];
    buf->read_pos = (buf->read_pos + 1U) % buf->capacity;
  }

  return count;
}
