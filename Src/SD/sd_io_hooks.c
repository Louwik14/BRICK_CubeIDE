#include "SD/sd_io_hooks.h"

#include "SD/sd_block_device.h"
#include "Storage/sd_access_gate.h"
#include "sdmmc.h"

uint8_t brick_sd_is_detected(void)
{
    /* There is no card-detect GPIO on LowCost. */
    return BSP_SD_IsDetected();
}

uint8_t brick_sd_init_failure_is_no_media(uint8_t bsp_status)
{
    if ((bsp_status == MSD_ERROR_SD_NOT_PRESENT)
        || (bsp_status == (uint8_t)HAL_TIMEOUT))
    {
        return 1U;
    }

    /* Failed CMD55/ACMD41 exchanges may be collapsed by the local H7 HAL to
       UNSUPPORTED_FEATURE. Keep CRC, parameter, DMA and mixed errors fatal. */
    const uint32_t no_media_errors = HAL_SD_ERROR_CMD_RSP_TIMEOUT
        | HAL_SD_ERROR_TIMEOUT | HAL_SD_ERROR_UNSUPPORTED_FEATURE;
    return ((hsd1.ErrorCode != HAL_SD_ERROR_NONE)
            && ((hsd1.ErrorCode & ~no_media_errors) == 0U)) ? 1U : 0U;
}

void brick_sd_generated_sdmmc_error_handler(void)
{
    if (brick_sd_init_failure_is_no_media(MSD_ERROR) == 0U)
    {
        Error_Handler();
    }
}

void brick_sd_media_fault(void)
{
    sd_access_fs_invalidate_mount();
}

uint8_t brick_sd_read_blocks_dma(uint32_t *data,
                                 uint32_t block_idx,
                                 uint32_t blocks_nbr)
{
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
