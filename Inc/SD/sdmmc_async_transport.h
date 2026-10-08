#ifndef SDMMC_ASYNC_TRANSPORT_H
#define SDMMC_ASYNC_TRANSPORT_H

#include <stdint.h>

#define SDMMC_ASYNC_PROGRESS_PAGE_BYTES  (16384U)
#define SDMMC_ASYNC_PROGRESS_CHUNK_BYTES (512U)
#define SDMMC_ASYNC_PROGRESS_CHUNKS \
    (SDMMC_ASYNC_PROGRESS_PAGE_BYTES / SDMMC_ASYNC_PROGRESS_CHUNK_BYTES)
#define SDMMC_ASYNC_PROGRESS_MAX_BYTES   (65536U)
#define SDMMC_ASYNC_PROGRESS_MAX_CHUNKS \
    (SDMMC_ASYNC_PROGRESS_MAX_BYTES / SDMMC_ASYNC_PROGRESS_CHUNK_BYTES)
#define SDMMC_ASYNC_PREPARED_FLAG_PROGRESSIVE (1U << 0)

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

typedef uint8_t *(*sdmmc_async_chunk_address_isr_t)(
    void *context, uint32_t chunk_index);

void sdmmc_async_transport_init(void);
uint8_t sdmmc_async_transport_start_read(void *dst,
                                         uint32_t lba,
                                         uint32_t sector_count);
uint8_t sdmmc_async_transport_start_read_progressive(void *dst,
                                                     uint32_t lba,
                                                     uint32_t sector_count);
uint8_t sdmmc_async_transport_start_read_progressive_scatter(
    void *dst, uint32_t lba, uint32_t sector_count,
    sdmmc_async_chunk_address_isr_t chunk_address_isr,
    void *chunk_address_context);
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

#endif
