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
    SD_BLOCK_DEVICE_READ_FAIL,
    SD_BLOCK_DEVICE_QUEUE_FULL,
    SD_BLOCK_DEVICE_BUSY
} sd_block_device_result_t;

#define SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH (4U)

typedef struct
{
    uint32_t lba;
    uint32_t sector_count;
    void *dst;
    sd_block_device_result_t result;
} sd_block_device_async_completion_t;

void sd_block_device_async_init(void);
sd_block_device_result_t sd_block_device_async_enqueue(uint32_t lba,
                                                       uint32_t sector_count,
                                                       void *dst);
void sd_block_device_async_poll(void);
uint8_t sd_block_device_async_take_completion(
    sd_block_device_async_completion_t *out_completion);
uint32_t sd_block_device_async_pending_count(void);
void sd_block_device_async_cancel(void);
void sd_block_device_async_read_complete_isr(void);
void sd_block_device_async_error_isr(void);

#ifdef __cplusplus
}
#endif
