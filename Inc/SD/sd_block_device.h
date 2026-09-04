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
    SD_BLOCK_DEVICE_WRITE_FAIL,
    SD_BLOCK_DEVICE_QUEUE_FULL,
    SD_BLOCK_DEVICE_BUSY,
    SD_BLOCK_DEVICE_TIMEOUT,
    SD_BLOCK_DEVICE_MEDIA_CHANGED,
    SD_BLOCK_DEVICE_CARD_REMOVED,
    SD_BLOCK_DEVICE_ABORTED,
    SD_BLOCK_DEVICE_DMA_START_FAIL,
    SD_BLOCK_DEVICE_ABORT_FAILED
} sd_block_device_result_t;

#define SD_BLOCK_DEVICE_ASYNC_FIFO_DEPTH (4U)
#define SD_BLOCK_DEVICE_MAX_SECTORS_PER_TRANSFER (128U)

typedef enum
{
    SD_BLOCK_DEVICE_OPERATION_NONE = 0,
    SD_BLOCK_DEVICE_OPERATION_READ,
    SD_BLOCK_DEVICE_OPERATION_WRITE
} sd_block_device_operation_t;

typedef enum
{
    SD_BLOCK_DEVICE_HW_IDLE = 0,
    SD_BLOCK_DEVICE_HW_READ_DMA,
    SD_BLOCK_DEVICE_HW_READ_WAIT_CARD_READY,
    SD_BLOCK_DEVICE_HW_WRITE_DMA,
    SD_BLOCK_DEVICE_HW_WRITE_WAIT_CARD_READY,
    SD_BLOCK_DEVICE_HW_ABORTING,
    SD_BLOCK_DEVICE_HW_ERROR_LATCHED
} sd_block_device_hardware_state_t;

typedef struct
{
    uint32_t lba;
    uint32_t sector_count;
    void *dst;
    const void *src;
    uint32_t owner_generation;
    uint32_t media_epoch;
    uint32_t duration_ms;
    sd_block_device_operation_t operation;
    sd_block_device_result_t result;
    uint8_t owner_client;
} sd_block_device_async_completion_t;

typedef struct
{
    uint32_t write_submitted;
    uint32_t write_completed;
    uint32_t write_failed;
    uint32_t busy_rejects;
    uint32_t max_transaction_duration_ms;
    uint32_t max_card_ready_latency_ms;
    uint32_t abort_count;
    uint32_t read_to_write_switches;
    uint32_t write_to_read_switches;
} sd_block_device_async_metrics_t;

void sd_block_device_async_init(void);
sd_block_device_result_t sd_block_device_async_enqueue(uint32_t lba,
                                                       uint32_t sector_count,
                                                       void *dst);
sd_block_device_result_t sd_block_device_async_write_submit(
    uint32_t lba,
    uint32_t sector_count,
    const void *src,
    uint32_t owner_generation);
void sd_block_device_async_poll(void);
uint8_t sd_block_device_async_take_completion(
    sd_block_device_async_completion_t *out_completion);
uint32_t sd_block_device_async_pending_count(void);
uint8_t sd_block_device_async_immediate_pending(void);
uint8_t sd_block_device_async_write_buffer_locked(const void *src);
sd_block_device_hardware_state_t sd_block_device_async_hardware_state(void);
sd_block_device_result_t sd_block_device_async_abort_active(void);
void sd_block_device_async_cancel(void);
void sd_block_device_async_metrics_reset(void);
void sd_block_device_async_metrics_get(sd_block_device_async_metrics_t *out_metrics);
void sd_block_device_async_read_complete_isr(void);
void sd_block_device_async_write_complete_isr(void);
void sd_block_device_async_abort_complete_isr(void);
void sd_block_device_async_error_isr(void);

#ifdef __cplusplus
}
#endif
