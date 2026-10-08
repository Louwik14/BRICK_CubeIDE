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

typedef uint8_t (*sd_block_device_read_progress_isr_t)(
    void *context, uint32_t available_bytes, uint32_t publish_cycles);

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

/* Stable zero-initialized caller-owned request. It must outlive
 * completion/abort drainage. */
typedef struct
{
    uint32_t lba;
    uint32_t sector_count;
    uint8_t *buffer;
    uint32_t owner_generation;
    uint32_t media_epoch;
    uint32_t queued_tick;
    uint32_t start_tick;
    uint32_t perf_submit_enter_cycles;
    uint32_t perf_submit_cycles;
    uint32_t perf_launch_enter_cycles;
    uint32_t perf_pre_cache_start_cycles;
    uint32_t perf_pre_cache_end_cycles;
    uint32_t perf_command_cycles;
    uint32_t perf_data_start_cycles;
    uint32_t perf_data_end_cycles;
    uint32_t perf_dma_cycles;
    uint32_t perf_complete_cycles;
    uint32_t perf_cache_start_cycles;
    uint32_t perf_cache_end_cycles;
    uint32_t perf_publish_cycles;
    uint32_t token;
    sd_block_device_operation_t operation;
    sd_block_device_result_t result;
    sd_block_device_result_t abort_result;
    uint8_t owner_client;
    uint8_t started;
    uint8_t callback_seen;
    uint8_t completed;
    uint8_t read_destination_cpu_clean;
    volatile uint8_t irq_complete;
    volatile uint8_t irq_error;
    uint8_t prepared;
    uint8_t chained_next;
    uint8_t queued;
    uint8_t progressive_chunks_invalidated;
    sd_block_device_read_progress_isr_t progress_isr;
    void *progress_context;
} sd_block_device_async_request_t;

void sd_block_device_async_init(void);
sd_block_device_result_t sd_block_device_async_enqueue(uint32_t lba,
                                                       uint32_t sector_count,
                                                       void *dst);
sd_block_device_result_t sd_block_device_async_read_submit(
    uint32_t lba,
    uint32_t sector_count,
    void *dst,
    uint32_t owner_generation);
/* RX fast path: every destination cacheline is known free of CPU dirty data
 * and the sector transfer overwrites complete, aligned cachelines.  The
 * normal post-DMA invalidate remains mandatory. */
sd_block_device_result_t sd_block_device_async_read_submit_cpu_clean(
    uint32_t lba, uint32_t sector_count, void *dst,
    uint32_t owner_generation);
sd_block_device_result_t sd_block_device_async_read_submit_request(
    sd_block_device_async_request_t *request, uint32_t lba,
    uint32_t sector_count, void *dst, uint32_t owner_generation,
    uint8_t destination_cpu_clean);
sd_block_device_result_t sd_block_device_async_read_submit_progressive_request(
    sd_block_device_async_request_t *request, uint32_t lba,
    uint32_t sector_count, void *dst, uint32_t owner_generation,
    uint8_t destination_cpu_clean,
    sd_block_device_read_progress_isr_t progress_isr,
    void *progress_context);
sd_block_device_result_t sd_block_device_async_write_submit(
    uint32_t lba,
    uint32_t sector_count,
    const void *src,
    uint32_t owner_generation);
void sd_block_device_async_poll(void);
uint8_t sd_block_device_async_take_completion(
    sd_block_device_async_completion_t *out_completion);
uint8_t sd_block_device_async_take_completion_request(
    sd_block_device_async_request_t **out_request);
uint32_t sd_block_device_async_pending_count(void);
uint8_t sd_block_device_async_write_buffer_locked(const void *src);
sd_block_device_hardware_state_t sd_block_device_async_hardware_state(void);
typedef struct
{
    uint8_t pending;
    uint8_t operation;
    uint8_t owner_client;
    uint8_t fault_latched;
    uint8_t irq_error;
    uint8_t hardware_state;
    uint8_t progressive_chunks_invalidated;
    uint8_t reserved;
    uint32_t result;
} sd_block_device_debug_snapshot_t;
void sd_block_device_debug_snapshot(sd_block_device_debug_snapshot_t *out);
sd_block_device_result_t sd_block_device_async_abort_active(void);
sd_block_device_result_t sd_block_device_async_abort_generation(
    uint32_t owner_generation);
void sd_block_device_async_cancel(void);
void sd_block_device_async_invalidate_prepared(void);
void sd_block_device_async_read_complete_isr(void);
void sd_block_device_async_read_chunk_isr(uint32_t chunk_index);
void sd_block_device_async_write_complete_isr(void);
void sd_block_device_async_abort_complete_isr(void);
void sd_block_device_async_error_isr(void);

#ifdef __cplusplus
}
#endif
