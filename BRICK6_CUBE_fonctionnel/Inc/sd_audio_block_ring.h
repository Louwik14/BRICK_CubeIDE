#ifndef SD_AUDIO_BLOCK_RING_H
#define SD_AUDIO_BLOCK_RING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_BLOCK_SIZE   4096U
#define AUDIO_BLOCK_COUNT  4U

typedef struct
{
  uint8_t blocks[AUDIO_BLOCK_COUNT][AUDIO_BLOCK_SIZE];
  volatile uint32_t write_index;
  volatile uint32_t read_index;
  volatile uint32_t fill_level;
} audio_block_ring_t;

extern audio_block_ring_t sd_audio_block_ring;

void audio_block_ring_init(audio_block_ring_t *ring);
uint8_t *audio_block_ring_get_write_ptr(audio_block_ring_t *ring);
void audio_block_ring_produce(audio_block_ring_t *ring);
uint8_t *audio_block_ring_get_read_ptr(audio_block_ring_t *ring);
void audio_block_ring_consume(audio_block_ring_t *ring);
uint32_t audio_block_ring_fill_level(const audio_block_ring_t *ring);

#ifdef __cplusplus
}
#endif

#endif /* SD_AUDIO_BLOCK_RING_H */
