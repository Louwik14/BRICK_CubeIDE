#include "SD/sd_io_hooks.h"

#include <string.h>

#include "SD/sd_block_device.h"
#include "Storage/sd_access_gate.h"

#define SD_IO_HOOK_SECTOR_BYTES (512U)

static volatile sd_diskio_read_metrics_t g_sd_diskio_read_metrics;

uint8_t brick_sd_is_detected(void)
{
    const uint8_t detected = BSP_SD_IsDetected();
    sd_access_media_set_present((detected == SD_PRESENT) ? 1U : 0U);
    return detected;
}

void brick_sd_media_fault(void)
{
    sd_access_fs_invalidate_mount();
}

uint8_t brick_sd_read_blocks_dma(uint32_t *data,
                                 uint32_t block_idx,
                                 uint32_t blocks_nbr)
{
    sd_diskio_read_metrics_note_blocks(blocks_nbr);
    return BSP_SD_ReadBlocks_DMA(data, block_idx, blocks_nbr);
}

uint8_t brick_sd_write_blocks_dma(const uint32_t *data,
                                  uint32_t block_idx,
                                  uint32_t blocks_nbr)
{
    return BSP_SD_WriteBlocks_DMA((uint32_t *)(uintptr_t)data,
                                  block_idx,
                                  blocks_nbr);
}

void brick_sd_async_read_complete_isr(void)
{
    sd_block_device_async_read_complete_isr();
}

void brick_sd_async_write_complete_isr(void)
{
    sd_block_device_async_write_complete_isr();
}

void brick_sd_async_abort_complete_isr(void)
{
    sd_block_device_async_abort_complete_isr();
}

void brick_sd_async_error_isr(void)
{
    sd_block_device_async_error_isr();
}

void sd_diskio_read_metrics_reset(void)
{
    memset((void *)&g_sd_diskio_read_metrics, 0,
           sizeof(g_sd_diskio_read_metrics));
}

void sd_diskio_read_metrics_get(sd_diskio_read_metrics_t *out_metrics)
{
    if (out_metrics != 0)
    {
        *out_metrics = g_sd_diskio_read_metrics;
    }
}

void sd_diskio_read_metrics_note_blocks(uint32_t blocks)
{
    const uint32_t bytes = blocks * SD_IO_HOOK_SECTOR_BYTES;
    g_sd_diskio_read_metrics.read_transactions++;
    g_sd_diskio_read_metrics.read_bytes += bytes;
    if (bytes > g_sd_diskio_read_metrics.max_read_bytes)
    {
        g_sd_diskio_read_metrics.max_read_bytes = bytes;
    }
}
