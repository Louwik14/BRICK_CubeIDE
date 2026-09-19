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
    sd_block_device_operation_t operation;
    sd_block_device_result_t result;
    uint8_t owner_client;
} sd_block_device_async_completion_t;

/* Temporary streamer latency instrumentation.  Write state=1 from GDB to
 * reset/start, state=0 to stop.  The implementation changes state to 2 while
 * collecting. */
typedef struct
{
    uint32_t calls;
    uint32_t total_cycles_lo;
    uint32_t total_cycles_hi;
    uint32_t max_cycles;
} sd_stream_latency_metric_t;

typedef struct
{
    volatile uint32_t state;
    uint32_t reserved;
    sd_stream_latency_metric_t dma_physical;
    sd_stream_latency_metric_t complete_to_worker;
    sd_stream_latency_metric_t worker_to_next_dma;
    sd_stream_latency_metric_t refill_total;
    uint32_t sectors_total_lo;
    uint32_t sectors_total_hi;
    uint32_t bytes_total_lo;
    uint32_t bytes_total_hi;
    sd_stream_latency_metric_t pending_ready_to_next_dma;
    uint32_t dma_complete_with_other_refill_pending;
    uint32_t dma_complete_without_other_refill_pending;
} sd_stream_latency_diag_t;

typedef struct
{
    volatile uint32_t armed;
    volatile uint32_t chained;
    volatile uint32_t invalidated;
} sd_block_device_nplus1_diag_t;

extern volatile sd_stream_latency_diag_t g_sd_stream_latency_diag;
extern volatile sd_block_device_nplus1_diag_t g_sd_block_device_nplus1_diag;
void sd_stream_latency_refill_begin(uint16_t slot_index);
void sd_stream_latency_refill_ready(uint16_t slot_index);
void sd_stream_latency_dma_followup(uint8_t pending);

void sd_block_device_async_init(void);
sd_block_device_result_t sd_block_device_async_enqueue(uint32_t lba,
                                                       uint32_t sector_count,
                                                       void *dst);
sd_block_device_result_t sd_block_device_async_read_submit(
    uint32_t lba,
    uint32_t sector_count,
    void *dst,
    uint32_t owner_generation);
sd_block_device_result_t sd_block_device_async_write_submit(
    uint32_t lba,
    uint32_t sector_count,
    const void *src,
    uint32_t owner_generation);
void sd_block_device_async_poll(void);
uint8_t sd_block_device_async_take_completion(
    sd_block_device_async_completion_t *out_completion);
uint32_t sd_block_device_async_pending_count(void);
uint8_t sd_block_device_async_write_buffer_locked(const void *src);
sd_block_device_hardware_state_t sd_block_device_async_hardware_state(void);
sd_block_device_result_t sd_block_device_async_abort_active(void);
sd_block_device_result_t sd_block_device_async_abort_generation(
    uint32_t owner_generation);
void sd_block_device_async_cancel(void);
void sd_block_device_async_invalidate_prepared(void);
void sd_block_device_async_read_complete_isr(void);
void sd_block_device_async_write_complete_isr(void);
void sd_block_device_async_abort_complete_isr(void);
void sd_block_device_async_error_isr(void);

#ifdef __cplusplus
}
#endif
