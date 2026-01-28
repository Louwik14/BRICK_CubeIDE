#include "sd_audio_block_ring.h"

static uint32_t audio_block_ring_advance(uint32_t index)
{
  return (index + 1U) % AUDIO_BLOCK_COUNT;
}

audio_block_ring_t sd_audio_block_ring;

void audio_block_ring_init(audio_block_ring_t *ring)
{
  if (ring == NULL)
  {
    return;
  }

  ring->write_index = 0U;
  ring->read_index = 0U;
  ring->fill_level = 0U;
}

uint8_t *audio_block_ring_get_write_ptr(audio_block_ring_t *ring)
{
  if ((ring == NULL) || (ring->fill_level >= AUDIO_BLOCK_COUNT))
  {
    return NULL;
  }

  return ring->blocks[ring->write_index];
}

void audio_block_ring_produce(audio_block_ring_t *ring)
{
  if ((ring == NULL) || (ring->fill_level >= AUDIO_BLOCK_COUNT))
  {
    return;
  }

  ring->write_index = audio_block_ring_advance(ring->write_index);
  ring->fill_level++;
}

uint8_t *audio_block_ring_get_read_ptr(audio_block_ring_t *ring)
{
  if ((ring == NULL) || (ring->fill_level == 0U))
  {
    return NULL;
  }

  return ring->blocks[ring->read_index];
}

void audio_block_ring_consume(audio_block_ring_t *ring)
{
  if ((ring == NULL) || (ring->fill_level == 0U))
  {
    return;
  }

  ring->read_index = audio_block_ring_advance(ring->read_index);
  ring->fill_level--;
}

uint32_t audio_block_ring_fill_level(const audio_block_ring_t *ring)
{
  if (ring == NULL)
  {
    return 0U;
  }

  return ring->fill_level;
}
