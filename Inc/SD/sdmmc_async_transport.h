#ifndef SDMMC_ASYNC_TRANSPORT_H
#define SDMMC_ASYNC_TRANSPORT_H

#include <stdint.h>

typedef enum
{
    SDMMC_ASYNC_STATE_IDLE = 0,
    SDMMC_ASYNC_STATE_CMD_START,
    SDMMC_ASYNC_STATE_DATA_ACTIVE,
    SDMMC_ASYNC_STATE_WAIT_CMD12,
    SDMMC_ASYNC_STATE_COMPLETE,
    SDMMC_ASYNC_STATE_ERROR
} sdmmc_async_state_t;

typedef enum
{
    SDMMC_ASYNC_EVENT_NONE = 0,
    SDMMC_ASYNC_EVENT_READ_CHUNK,
    SDMMC_ASYNC_EVENT_READ_COMPLETE,
    SDMMC_ASYNC_EVENT_WRITE_COMPLETE,
    SDMMC_ASYNC_EVENT_ERROR
} sdmmc_async_event_t;

typedef enum
{
    SDMMC_ASYNC_PREPARED_EMPTY = 0,
    SDMMC_ASYNC_PREPARED_WRITING,
    SDMMC_ASYNC_PREPARED_READY,
    SDMMC_ASYNC_PREPARED_ACTIVE,
    SDMMC_ASYNC_PREPARED_DONE,
    SDMMC_ASYNC_PREPARED_ERROR
} sdmmc_async_prepared_state_t;

typedef enum
{
    SDMMC_ASYNC_CHAIN_NONE = 0,
    SDMMC_ASYNC_CHAIN_STARTED,
    SDMMC_ASYNC_CHAIN_ERROR
} sdmmc_async_chain_result_t;

#define SDMMC_ASYNC_PROGRESS_TRACE_COUNT (8U)

typedef struct
{
    uint32_t irq_index;
    uint32_t sta;
    uint32_t mask;
    uint32_t idmabase0;
    uint32_t idmabase1;
    uint32_t idmabsize;
    uint32_t dcount;
    uint32_t dlen;
    uint8_t progressive_chunk_index;
    uint8_t idmabact;
    uint8_t completed_buffer_hw;
    uint8_t expected_completed_buffer;
} sdmmc_async_progress_trace_t;

typedef struct
{
    volatile uint32_t idmabtc_count;
    volatile uint32_t cmd18_count;
    volatile uint32_t cmd12_count;
    volatile uint32_t rearm_count;
    volatile uint32_t rearm_cycles_sum;
    volatile uint32_t average_rearm_cycles;
    volatile uint32_t max_rearm_cycles;
    volatile uint32_t max_rearm_us;
    volatile uint32_t rearm_deadline_miss_count;
    volatile uint32_t dataend_count;
    volatile uint32_t final_chunk_idmabtc_count;
    volatile uint32_t configured_idmabndt;
    volatile uint32_t configured_chunk_bytes;
    volatile uint32_t expected_idma_buffers;
    volatile uint32_t trace_count;
    sdmmc_async_progress_trace_t trace[SDMMC_ASYNC_PROGRESS_TRACE_COUNT];
} sdmmc_async_progress_diag_t;

extern volatile sdmmc_async_progress_diag_t g_sdmmc_async_progress_diag;

typedef struct
{
    uint32_t lba;
    uint32_t sector_count;
    void *buffer;
    uint32_t token;
    uint32_t owner_generation;
    uint32_t media_epoch;
    uint8_t owner;
    uint8_t operation;
    uint8_t flags;
} sdmmc_async_prepared_transfer_t;

void sdmmc_async_transport_init(void);
uint8_t sdmmc_async_transport_start_read(void *dst,
                                         uint32_t lba,
                                         uint32_t sector_count);
uint8_t sdmmc_async_transport_start_read_progressive(void *dst,
                                                     uint32_t lba,
                                                     uint32_t sector_count);
uint8_t sdmmc_async_transport_start_write(const void *src,
                                          uint32_t lba,
                                          uint32_t sector_count);
uint8_t sdmmc_async_transport_arm_next(
    const sdmmc_async_prepared_transfer_t *transfer);
sdmmc_async_chain_result_t sdmmc_async_transport_chain_next(
    uint32_t *out_token);
uint8_t sdmmc_async_transport_invalidate_next(void);
sdmmc_async_prepared_state_t sdmmc_async_transport_prepared_state(void);
uint8_t sdmmc_async_transport_irq_owned(void);
sdmmc_async_event_t sdmmc_async_transport_irq_handler(void);
uint8_t sdmmc_async_transport_take_completed_chunk(uint32_t *out_chunk_index);
uint8_t sdmmc_async_transport_release_complete(void);
uint8_t sdmmc_async_transport_abort(void);
sdmmc_async_state_t sdmmc_async_transport_state(void);
uint32_t sdmmc_async_transport_error(void);
uint32_t sdmmc_async_transport_command_cycles(void);
uint32_t sdmmc_async_transport_data_start_cycles(void);
uint32_t sdmmc_async_transport_data_end_cycles(void);

#endif
