#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SD_BLOCK_DEVICE_OK = 0,
    SD_BLOCK_DEVICE_INVALID_ARG,
    SD_BLOCK_DEVICE_ISR_CONTEXT,
    SD_BLOCK_DEVICE_GATE_NOT_HELD,
    SD_BLOCK_DEVICE_READ_FAIL
} sd_block_device_result_t;

sd_block_device_result_t sd_block_device_read(uint32_t lba,
                                              uint32_t sector_count,
                                              void *dst);

#ifdef __cplusplus
}
#endif
