#ifndef TEST_SD_IO_HOOKS_H
#define TEST_SD_IO_HOOKS_H

#include <stdint.h>

uint8_t brick_sd_is_detected(void);
void brick_sd_media_fault(void);
uint8_t brick_sd_read_blocks_dma(uint32_t *data,
                                 uint32_t block_idx,
                                 uint32_t blocks_nbr);
uint8_t brick_sd_write_blocks_dma(const uint32_t *data,
                                  uint32_t block_idx,
                                  uint32_t blocks_nbr);

#endif
