#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_contract.h"
#include "Sampler/sample_stream_time.h"
#include "Sampler/sample_stream_trace.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_REQUEST_QUEUE_CAPACITY SAMPLE_PAGE_MAX_COUNT

typedef struct
{
    sample_audio_key_t key;
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint32_t registration_epoch;
    uint32_t page_index;
    uint32_t requested_at;
    sample_stream_audio_frame_t created_audio_frame;
    sample_stream_audio_frame_t consume_deadline_audio_frame;
    uint32_t owner_generation;
    uint16_t sample_id;
    uint16_t reserved;
    uint8_t active;
    uint8_t priority;
    uint8_t owner_kind;
    uint8_t owner_id;
    uint8_t role;
#if BRICK6_STREAM_TRACE
    uint8_t trace_slot;
    uint8_t trace_valid;
#endif
} sample_stream_request_entry_t;

typedef enum
{
    SAMPLE_STREAM_REQUEST_INSERTED = 0,
    SAMPLE_STREAM_REQUEST_MERGED,
    SAMPLE_STREAM_REQUEST_FULL
} sample_stream_request_publish_result_t;

typedef struct
{
    sample_audio_format_t format;
    uint16_t stride_floats;
    uint32_t frames_per_page;
} sample_stream_request_geometry_t;

void sample_stream_request_queue_init(void);
sample_stream_request_entry_t *sample_stream_request_queue_entries(void);
const sample_stream_request_entry_t *sample_stream_request_queue_entries_const(void);
sample_stream_request_publish_result_t sample_stream_request_queue_publish(
    const sample_stream_request_contract_t *request,
    const sample_stream_request_geometry_t *geometry,
    sample_stream_request_entry_t **out_entry);

#ifdef __cplusplus
}
#endif
