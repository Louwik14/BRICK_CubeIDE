#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SD_BLOCK_DEVICE_SECTOR_BYTES (512U)
#define SD_BLOCK_DEVICE_DMA_ALIGNMENT_BYTES (32U)
#define SD_BLOCK_DEVICE_MAX_SECTORS_PER_READ (128U)

typedef enum
{
    SD_BLOCK_DEVICE_OK = 0,
    SD_BLOCK_DEVICE_ERR_PARAM,
    SD_BLOCK_DEVICE_ERR_GATE,
    SD_BLOCK_DEVICE_ERR_READ,
    SD_BLOCK_DEVICE_ERR_TIMEOUT,
    SD_BLOCK_DEVICE_ERR_IRQ
} sd_block_device_result_t;

typedef struct
{
    uint32_t sector_read_ok;
    uint32_t sector_read_fail;
    uint32_t alignment_reject_count;
    uint32_t param_reject_count;
    uint32_t gate_reject_count;
    uint32_t irq_reject_count;
    uint32_t last_lba;
    uint32_t last_sector_count;
    uint32_t last_result;
    uint32_t last_sd_error;
} sd_block_device_diag_snapshot_t;

/*
 * Caller-owned gate contract:
 * - caller must already hold sd_access_gate;
 * - this wrapper does not acquire or release a SD client;
 * - never call from audio IRQ or any ISR.
 */
sd_block_device_result_t sd_block_device_read_sectors(uint32_t first_lba,
                                                      uint32_t sector_count,
                                                      void *dst_aligned,
                                                      uint32_t dst_size_bytes);
void sd_block_device_diag_get_snapshot(sd_block_device_diag_snapshot_t *out_snapshot);
void sd_block_device_diag_reset(void);

#ifdef __cplusplus
}
#endif
