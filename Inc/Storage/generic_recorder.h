#pragma once

#include <stdint.h>

#include "SD/sd_scheduler.h"
#include "Storage/recorder_file_reservation.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GENERIC_RECORDER_WRITE_BUFFER_COUNT (2U)

typedef enum
{
    GENERIC_RECORDER_IDLE = 0,
    GENERIC_RECORDER_CAPTURING,
    GENERIC_RECORDER_DRAINING,
    GENERIC_RECORDER_FINALIZABLE,
    GENERIC_RECORDER_ERROR,
    GENERIC_RECORDER_ABORTED
} generic_recorder_state_t;

typedef enum
{
    GENERIC_RECORDER_ERROR_NONE = 0,
    GENERIC_RECORDER_ERROR_INVALID_ARGUMENT,
    GENERIC_RECORDER_ERROR_INVALID_STATE,
    GENERIC_RECORDER_ERROR_RING_FULL,
    GENERIC_RECORDER_ERROR_RESERVATION,
    GENERIC_RECORDER_ERROR_NO_SPACE,
    GENERIC_RECORDER_ERROR_MAPPING,
    GENERIC_RECORDER_ERROR_WRITE,
    GENERIC_RECORDER_ERROR_MEDIA_CHANGED,
    GENERIC_RECORDER_ERROR_TRANSPORT
} generic_recorder_error_t;

typedef enum
{
    GENERIC_RECORDER_TRANSPORT_STARTED = 0,
    GENERIC_RECORDER_TRANSPORT_BUSY,
    GENERIC_RECORDER_TRANSPORT_ERROR
} generic_recorder_transport_start_t;

typedef enum
{
    GENERIC_RECORDER_TRANSPORT_ACTIVE = 0,
    GENERIC_RECORDER_TRANSPORT_COMPLETED,
    GENERIC_RECORDER_TRANSPORT_FAILED,
    GENERIC_RECORDER_TRANSPORT_MEDIA_CHANGED,
    GENERIC_RECORDER_TRANSPORT_RECOVERY_ABORT
} generic_recorder_transport_poll_t;

typedef struct
{
    uint32_t lba;
    uint32_t sector_count;
    const void *buffer;
    uint32_t owner_generation;
    uint32_t media_epoch;
} generic_recorder_transport_completion_t;

typedef generic_recorder_transport_start_t (*generic_recorder_transport_start_fn)(
    void *context,
    uint32_t lba,
    uint32_t sector_count,
    const void *buffer,
    uint32_t owner_generation,
    uint32_t media_epoch);
typedef generic_recorder_transport_poll_t (*generic_recorder_transport_poll_fn)(
    void *context,
    generic_recorder_transport_completion_t *completion);

typedef struct
{
    void *context;
    generic_recorder_transport_start_fn start;
    generic_recorder_transport_poll_fn poll;
} generic_recorder_transport_t;

typedef uint8_t (*generic_recorder_reservation_snapshot_fn)(
    void *context,
    recorder_file_reservation_map_snapshot_t *snapshot);
typedef uint8_t (*generic_recorder_reservation_resolve_fn)(
    void *context,
    const recorder_file_reservation_map_snapshot_t *snapshot,
    uint64_t file_byte_offset,
    uint32_t requested_bytes,
    sample_stream_physical_span_t *span);
typedef recorder_file_reservation_result_t (*generic_recorder_reservation_extend_fn)(
    void *context,
    uint64_t additional_bytes);

typedef struct
{
    void *context;
    generic_recorder_reservation_snapshot_fn snapshot;
    generic_recorder_reservation_resolve_fn resolve;
    generic_recorder_reservation_extend_fn extend;
} generic_recorder_reservation_t;

typedef struct
{
    int32_t *ring_interleaved;
    uint32_t ring_capacity_frames;
    uint8_t *write_buffers[GENERIC_RECORDER_WRITE_BUFFER_COUNT];
    uint32_t write_buffer_bytes;
    uint32_t minimum_write_bytes;
    uint32_t sample_rate_hz;
    uint16_t channels;
    uint16_t reserved_header_bytes;
    uint32_t extension_bytes;
    uint32_t reservation_low_margin_us;
    uint32_t reservation_critical_margin_us;
    uint32_t estimated_write_us_per_sector;
    void (*critical_enter)(void *context);
    void (*critical_exit)(void *context);
    void *critical_context;
    generic_recorder_transport_t transport;
    generic_recorder_reservation_t reservation;
} generic_recorder_config_t;

typedef struct
{
    uint64_t frames_accepted;
    uint64_t bytes_accepted;
    uint64_t bytes_assigned;
    uint64_t bytes_committed;
    uint32_t ring_high_watermark_frames;
    uint32_t ring_min_free_frames;
    uint64_t reservation_min_margin_bytes;
    uint32_t extensions_requested;
    uint32_t extensions_completed;
    uint32_t extensions_failed;
    uint32_t write_candidates;
    uint32_t writes_submitted;
    uint32_t writes_completed;
    uint32_t writes_failed;
    uint64_t write_bytes_submitted;
    uint32_t max_write_bytes;
    uint32_t extent_crossings;
    uint64_t bytes_packed;
    uint64_t max_backlog_bytes;
    uint32_t ring_full_rejects;
    uint32_t stop_drain_duration_us;
} generic_recorder_metrics_t;

typedef struct
{
    generic_recorder_state_t state;
    generic_recorder_error_t error;
    uint64_t accepted_tail;
    uint64_t assigned_tail;
    uint64_t committed_tail;
    uint64_t reserved_capacity;
    uint64_t ring_margin_us;
    uint64_t reservation_margin_us;
    uint8_t accepting;
    uint8_t fully_committed;
} generic_recorder_status_t;

typedef enum
{
    GENERIC_RECORDER_DESCRIPTOR_FREE = 0,
    GENERIC_RECORDER_DESCRIPTOR_READY,
    GENERIC_RECORDER_DESCRIPTOR_IN_FLIGHT,
    GENERIC_RECORDER_DESCRIPTOR_FAILED
} generic_recorder_descriptor_state_t;

typedef struct
{
    uint8_t *buffer;
    uint64_t logical_offset;
    uint32_t lba;
    uint32_t dma_bytes;
    uint32_t valid_bytes;
    uint32_t sent_dma_bytes;
    uint32_t sent_valid_bytes;
    uint32_t active_dma_bytes;
    uint32_t active_valid_bytes;
    uint32_t map_generation;
    uint32_t media_epoch;
    uint16_t extent_index;
    generic_recorder_descriptor_state_t state;
} generic_recorder_write_descriptor_t;

typedef struct
{
    generic_recorder_config_t config;
    generic_recorder_metrics_t metrics;
    generic_recorder_write_descriptor_t descriptors[GENERIC_RECORDER_WRITE_BUFFER_COUNT];
    uint64_t accepted_tail;
    uint64_t assigned_tail;
    uint64_t committed_tail;
    uint64_t accepted_frames;
    uint64_t reserved_capacity;
    uint32_t media_epoch;
    uint32_t generation;
    uint32_t stop_started_us;
    uint16_t last_extent_index;
    generic_recorder_state_t state;
    generic_recorder_error_t error;
    uint8_t last_extent_valid;
    uint8_t extension_pending;
} generic_recorder_t;

void generic_recorder_init(generic_recorder_t *recorder);
uint8_t generic_recorder_begin(generic_recorder_t *recorder,
                               const generic_recorder_config_t *config);
uint8_t generic_recorder_push(generic_recorder_t *recorder,
                              const int32_t *pcm_interleaved,
                              uint32_t frames);
uint8_t generic_recorder_request_stop(generic_recorder_t *recorder,
                                      uint32_t now_us);
void generic_recorder_service(generic_recorder_t *recorder, uint32_t now_us);
void generic_recorder_abort(generic_recorder_t *recorder);
uint8_t generic_recorder_invariants_hold(const generic_recorder_t *recorder);
uint64_t generic_recorder_ring_margin_us(const generic_recorder_t *recorder);
uint64_t generic_recorder_reservation_margin_us(const generic_recorder_t *recorder);
void generic_recorder_get_status(const generic_recorder_t *recorder,
                                 generic_recorder_status_t *status);
void generic_recorder_get_metrics(const generic_recorder_t *recorder,
                                  generic_recorder_metrics_t *metrics);
sd_scheduler_provider_t generic_recorder_write_provider(generic_recorder_t *recorder);
sd_scheduler_provider_t generic_recorder_filesystem_provider(generic_recorder_t *recorder);

#ifdef __cplusplus
}
#endif
