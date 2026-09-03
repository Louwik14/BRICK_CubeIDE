#include "SD/sd_io_hooks.h"

#include <string.h>

#include "SD/sd_block_device.h"
#include "Storage/sd_access_gate.h"
#include "sdmmc.h"

#define SD_IO_HOOK_SECTOR_BYTES (512U)

static volatile sd_diskio_read_metrics_t g_sd_diskio_read_metrics;

uint8_t brick_sd_is_detected(void)
{
    /* BRICK has no card-detect GPIO.  This is the BSP's last successful
       initialization state, not a physical hotplug signal. */
    return BSP_SD_IsDetected();
}

uint8_t brick_sd_init_failure_is_no_media(uint8_t bsp_status)
{
    if (bsp_status == MSD_ERROR_SD_NOT_PRESENT)
    {
        return 1U;
    }
    if (bsp_status == (uint8_t)HAL_TIMEOUT)
    {
        return 1U;
    }

    /* BRICK has no card-detect GPIO, so HAL_SD_Init() communication is the
       media probe.  This HAL maps a failed CMD55/ACMD41 during SD_PowerON()
       to UNSUPPORTED_FEATURE, including the observed no-response/no-card
       path.  Keep CRC, parameter, DMA and other failures as real faults. */
    const uint32_t no_media_errors = HAL_SD_ERROR_CMD_RSP_TIMEOUT
        | HAL_SD_ERROR_TIMEOUT | HAL_SD_ERROR_UNSUPPORTED_FEATURE;
    return ((hsd1.ErrorCode != HAL_SD_ERROR_NONE)
            && ((hsd1.ErrorCode & ~no_media_errors) == 0U)) ? 1U : 0U;
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
