#include "Storage/sd_block_device.h"

#include <stddef.h>
#include <stdint.h>

#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "diskio.h"
#include "stm32h7xx_hal.h"

static CTRL_STATE sd_block_device_diag_snapshot_t g_sd_block_device_diag;

static void sd_block_device_note_reject(sd_block_device_result_t result)
{
    g_sd_block_device_diag.sector_read_fail++;
    g_sd_block_device_diag.last_result = (uint32_t)result;
}

sd_block_device_result_t sd_block_device_read_sectors(uint32_t first_lba,
                                                      uint32_t sector_count,
                                                      void *dst_aligned,
                                                      uint32_t dst_size_bytes)
{
    g_sd_block_device_diag.last_lba = first_lba;
    g_sd_block_device_diag.last_sector_count = sector_count;

    if (__get_IPSR() != 0U)
    {
        g_sd_block_device_diag.irq_reject_count++;
        sd_block_device_note_reject(SD_BLOCK_DEVICE_ERR_IRQ);
        return SD_BLOCK_DEVICE_ERR_IRQ;
    }

    if ((dst_aligned == 0) || (sector_count == 0U)
        || (sector_count > SD_BLOCK_DEVICE_MAX_SECTORS_PER_READ)
        || ((dst_size_bytes % SD_BLOCK_DEVICE_SECTOR_BYTES) != 0U)
        || (dst_size_bytes < (sector_count * SD_BLOCK_DEVICE_SECTOR_BYTES)))
    {
        g_sd_block_device_diag.param_reject_count++;
        sd_block_device_note_reject(SD_BLOCK_DEVICE_ERR_PARAM);
        return SD_BLOCK_DEVICE_ERR_PARAM;
    }

    if ((((uintptr_t)dst_aligned) & (SD_BLOCK_DEVICE_DMA_ALIGNMENT_BYTES - 1U)) != 0U)
    {
        g_sd_block_device_diag.alignment_reject_count++;
        sd_block_device_note_reject(SD_BLOCK_DEVICE_ERR_PARAM);
        return SD_BLOCK_DEVICE_ERR_PARAM;
    }

    if (sd_access_gate_is_owned() == 0U)
    {
        g_sd_block_device_diag.gate_reject_count++;
        sd_block_device_note_reject(SD_BLOCK_DEVICE_ERR_GATE);
        return SD_BLOCK_DEVICE_ERR_GATE;
    }

    const DRESULT read_result =
        disk_read(0U, (BYTE *)dst_aligned, (DWORD)first_lba, (UINT)sector_count);
    g_sd_block_device_diag.last_sd_error = (uint32_t)read_result;
    if (read_result != RES_OK)
    {
        sd_block_device_note_reject(SD_BLOCK_DEVICE_ERR_READ);
        return SD_BLOCK_DEVICE_ERR_READ;
    }

    g_sd_block_device_diag.sector_read_ok++;
    g_sd_block_device_diag.last_result = (uint32_t)SD_BLOCK_DEVICE_OK;
    return SD_BLOCK_DEVICE_OK;
}

void sd_block_device_diag_get_snapshot(sd_block_device_diag_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0)
    {
        return;
    }

    *out_snapshot = g_sd_block_device_diag;
}

void sd_block_device_diag_reset(void)
{
    g_sd_block_device_diag = (sd_block_device_diag_snapshot_t){0};
}
