#pragma once

#include <stdint.h>

#include "SD/sd_scheduler.h"
#include "Storage/recorder_file_reservation.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_RECORDER_WRITER_DESCRIPTOR_COUNT (2U)

typedef enum
{
    AUDIO_RECORDER_WRITER_IDLE = 0,
    AUDIO_RECORDER_WRITER_CAPTURING,
    AUDIO_RECORDER_WRITER_DRAINING,
    AUDIO_RECORDER_WRITER_FINALIZABLE,
    AUDIO_RECORDER_WRITER_ERROR,
    AUDIO_RECORDER_WRITER_ABORTED
} audio_recorder_writer_state_t;

typedef enum
{
    AUDIO_RECORDER_WRITER_ERROR_NONE = 0,
    AUDIO_RECORDER_WRITER_ERROR_INVALID_ARGUMENT,
    AUDIO_RECORDER_WRITER_ERROR_INVALID_STATE,
    AUDIO_RECORDER_WRITER_ERROR_RING_FULL,
    AUDIO_RECORDER_WRITER_ERROR_RESERVATION,
    AUDIO_RECORDER_WRITER_ERROR_NO_SPACE,
    AUDIO_RECORDER_WRITER_ERROR_MAPPING,
    AUDIO_RECORDER_WRITER_ERROR_WRITE,
    AUDIO_RECORDER_WRITER_ERROR_MEDIA_CHANGED,
    AUDIO_RECORDER_WRITER_ERROR_TRANSPORT
} audio_recorder_writer_error_t;

typedef enum
{
    AUDIO_RECORDER_WRITER_DESCRIPTOR_FREE = 0,
    AUDIO_RECORDER_WRITER_DESCRIPTOR_READY,
    AUDIO_RECORDER_WRITER_DESCRIPTOR_IN_FLIGHT,
    AUDIO_RECORDER_WRITER_DESCRIPTOR_FAILED
} audio_recorder_writer_descriptor_state_t;

typedef struct
{
    const uint8_t *buffer;
    uint64_t logical_offset;
    uint32_t lba;
    uint32_t dma_bytes;
    uint32_t valid_bytes;
    uint32_t sent_dma_bytes;
    uint32_t sent_valid_bytes;
    uint32_t active_dma_bytes;
    uint32_t active_valid_bytes;
    uint32_t media_epoch;
    audio_recorder_writer_descriptor_state_t state;
} audio_recorder_writer_write_descriptor_t;

typedef struct
{
    const void *ring_interleaved;
    uint8_t *tail_buffer;
    recorder_file_reservation_t *reservation;
    audio_recorder_writer_write_descriptor_t descriptors[AUDIO_RECORDER_WRITER_DESCRIPTOR_COUNT];
    uint64_t accepted_tail;
    uint64_t assigned_tail;
    uint64_t committed_tail;
    uint64_t accepted_frames;
    uint64_t released_frames;
    uint64_t reserved_capacity;
    uint32_t media_epoch;
    uint32_t generation;
    audio_recorder_writer_state_t state;
    audio_recorder_writer_error_t error;
    uint8_t extension_pending;
} audio_recorder_writer_t;

void audio_recorder_writer_init(audio_recorder_writer_t *recorder);
uint8_t audio_recorder_writer_begin(audio_recorder_writer_t *recorder,
                                    const void *ring_interleaved,
                                    uint8_t *tail_buffer,
                                    recorder_file_reservation_t *reservation);
uint8_t audio_recorder_writer_request_stop(audio_recorder_writer_t *recorder);
void audio_recorder_writer_service(audio_recorder_writer_t *recorder);
void audio_recorder_writer_abort(audio_recorder_writer_t *recorder);
sd_scheduler_provider_t audio_recorder_writer_write_provider(audio_recorder_writer_t *recorder);
sd_scheduler_provider_t audio_recorder_writer_filesystem_provider(audio_recorder_writer_t *recorder);

#ifdef __cplusplus
}
#endif
