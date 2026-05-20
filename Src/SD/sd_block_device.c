#include "SD/sd_block_device.h"

#include "Storage/sd_access_gate.h"

#include "diskio.h"
#include "stm32h7xx_hal.h"

sd_block_device_result_t sd_block_device_read(uint32_t lba,
                                              uint32_t sector_count,
                                              void *dst)
{
    if ((dst == 0) || (sector_count == 0U))
    {
        return SD_BLOCK_DEVICE_INVALID_ARG;
    }
    if (__get_IPSR() != 0U)
    {
        return SD_BLOCK_DEVICE_ISR_CONTEXT;
    }
    if (sd_access_gate_current_owner() == SD_ACCESS_CLIENT_NONE)
    {
        return SD_BLOCK_DEVICE_GATE_NOT_HELD;
    }

    const DRESULT result = disk_read(0U, (BYTE *)dst, (DWORD)lba, (UINT)sector_count);
    return (result == RES_OK) ? SD_BLOCK_DEVICE_OK : SD_BLOCK_DEVICE_READ_FAIL;
}
