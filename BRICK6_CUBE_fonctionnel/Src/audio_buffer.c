/**
 * @file audio_buffer.c
 * @brief Ring buffer int32 stocké en SDRAM (sans malloc).
 */

#include "audio_buffer.h"
#include "sdram.h"
#include "sdram_alloc.h"
#include "stm32h7xx_hal.h"

static uint32_t audio_buffer_advance(uint32_t index, uint32_t capacity)
{
  uint32_t next = index + 1U;
  return (next >= capacity) ? 0U : next;
}

static uint32_t audio_buffer_ptr_to_index(const void *ptr)
{
  return (uint32_t)(((uintptr_t)ptr - SDRAM_BANK_ADDR) / sizeof(uint32_t));
}

bool audio_buffer_init(audio_buffer_t *buffer, uint32_t capacity_samples, uint32_t alignment)
{
  if ((buffer == NULL) || (capacity_samples == 0U))
  {
    return false;
  }

  if (alignment < 4U)
  {
    alignment = 4U;
  }

  void *mem = SDRAM_Alloc(capacity_samples * sizeof(int32_t), alignment);
  if (mem == NULL)
  {
    return false;
  }

  buffer->base_index = audio_buffer_ptr_to_index(mem);
  buffer->capacity_samples = capacity_samples;
  buffer->write_pos = 0U;
  buffer->read_pos = 0U;
  buffer->fill_level = 0U;

  for (uint32_t i = 0; i < capacity_samples; ++i)
  {
    sdram_write32(buffer->base_index + i, 0U);
  }

  return true;
}

void audio_buffer_reset(audio_buffer_t *buffer)
{
  if (buffer == NULL)
  {
    return;
  }

  buffer->write_pos = 0U;
  buffer->read_pos = 0U;
  buffer->fill_level = 0U;
}

static uint32_t audio_buffer_enter_critical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void audio_buffer_exit_critical(uint32_t primask)
{
  __set_PRIMASK(primask);
}

uint32_t audio_buffer_write(audio_buffer_t *buffer, const int32_t *src, uint32_t samples)
{
  if ((buffer == NULL) || (src == NULL) || (samples == 0U))
  {
    return 0U;
  }

  uint32_t primask = audio_buffer_enter_critical();
  uint32_t free_space = buffer->capacity_samples - buffer->fill_level;
  if (samples > free_space)
  {
    samples = free_space;
  }

  for (uint32_t i = 0U; i < samples; ++i)
  {
    uint32_t index = buffer->base_index + buffer->write_pos;
    sdram_write32(index, (uint32_t)src[i]);
    buffer->write_pos = audio_buffer_advance(buffer->write_pos, buffer->capacity_samples);
  }

  buffer->fill_level += samples;
  audio_buffer_exit_critical(primask);
  return samples;
}

uint32_t audio_buffer_read(audio_buffer_t *buffer, int32_t *dst, uint32_t samples)
{
  if ((buffer == NULL) || (dst == NULL) || (samples == 0U))
  {
    return 0U;
  }

  uint32_t primask = audio_buffer_enter_critical();
  if (samples > buffer->fill_level)
  {
    samples = buffer->fill_level;
  }

  for (uint32_t i = 0U; i < samples; ++i)
  {
    uint32_t index = buffer->base_index + buffer->read_pos;
    dst[i] = (int32_t)sdram_read32(index);
    buffer->read_pos = audio_buffer_advance(buffer->read_pos, buffer->capacity_samples);
  }

  buffer->fill_level -= samples;
  audio_buffer_exit_critical(primask);
  return samples;
}

uint32_t audio_buffer_peek(const audio_buffer_t *buffer, int32_t *dst, uint32_t samples)
{
  if ((buffer == NULL) || (dst == NULL) || (samples == 0U))
  {
    return 0U;
  }

  if (samples > buffer->fill_level)
  {
    samples = buffer->fill_level;
  }

  uint32_t pos = buffer->read_pos;
  for (uint32_t i = 0U; i < samples; ++i)
  {
    uint32_t index = buffer->base_index + pos;
    dst[i] = (int32_t)sdram_read32(index);
    pos = audio_buffer_advance(pos, buffer->capacity_samples);
  }

  return samples;
}

uint32_t audio_buffer_skip(audio_buffer_t *buffer, uint32_t samples)
{
  if ((buffer == NULL) || (samples == 0U))
  {
    return 0U;
  }

  uint32_t primask = audio_buffer_enter_critical();
  if (samples > buffer->fill_level)
  {
    samples = buffer->fill_level;
  }

  for (uint32_t i = 0U; i < samples; ++i)
  {
    buffer->read_pos = audio_buffer_advance(buffer->read_pos, buffer->capacity_samples);
  }

  buffer->fill_level -= samples;
  audio_buffer_exit_critical(primask);
  return samples;
}

uint32_t audio_buffer_available(const audio_buffer_t *buffer)
{
  if (buffer == NULL)
  {
    return 0U;
  }

  return buffer->fill_level;
}

uint32_t audio_buffer_free(const audio_buffer_t *buffer)
{
  if (buffer == NULL)
  {
    return 0U;
  }

  return buffer->capacity_samples - buffer->fill_level;
}
