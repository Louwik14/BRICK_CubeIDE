#ifndef SD_IO_HOOKS_H
#define SD_IO_HOOKS_H

#include <stdint.h>

#include "SD/bsp_driver_sd.h"

typedef struct
{
    uint32_t read_transactions;
    uint32_t read_bytes;
    uint32_t max_read_bytes;
} sd_diskio_read_metrics_t;

uint8_t brick_sd_is_detected(void);
uint8_t brick_sd_init_failure_is_no_media(uint8_t bsp_status);
void brick_sd_media_fault(void);
uint8_t brick_sd_read_blocks_dma(uint32_t *data,
                                 uint32_t block_idx,
                                 uint32_t blocks_nbr);
uint8_t brick_sd_write_blocks_dma(const uint32_t *data,
                                  uint32_t block_idx,
                                  uint32_t blocks_nbr);
void brick_sd_async_read_complete_isr(void);
void brick_sd_async_write_complete_isr(void);
void brick_sd_async_abort_complete_isr(void);
void brick_sd_async_error_isr(void);

void sd_diskio_read_metrics_reset(void);
void sd_diskio_read_metrics_get(sd_diskio_read_metrics_t *out_metrics);
void sd_diskio_read_metrics_note_blocks(uint32_t blocks);

#endif
