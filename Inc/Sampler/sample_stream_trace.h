#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_key.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_STREAM_EVENT_TRACE_MAGIC       (0x53455654UL)
#define SAMPLE_STREAM_EVENT_TRACE_ABI_VERSION (2U)
#define SAMPLE_STREAM_EVENT_TRACE_CAPACITY    (128U)

typedef enum
{
    SAMPLE_STREAM_EVENT_SERVICE_BEGIN = 0,
    SAMPLE_STREAM_EVENT_SERVICE_END,
    SAMPLE_STREAM_EVENT_ADMISSION_ACCEPT,
    SAMPLE_STREAM_EVENT_ADMISSION_REJECT,
    SAMPLE_STREAM_EVENT_ADMISSION_RELEASE,
    SAMPLE_STREAM_EVENT_NEED_ADD,
    SAMPLE_STREAM_EVENT_NEED_DROP,
    SAMPLE_STREAM_EVENT_SELECT,
    SAMPLE_STREAM_EVENT_LOAD_BEGIN,
    SAMPLE_STREAM_EVENT_LOAD_END,
    SAMPLE_STREAM_EVENT_READY,
    SAMPLE_STREAM_EVENT_LOAD_ERROR,
    SAMPLE_STREAM_EVENT_CONSUME_MISS,
    SAMPLE_STREAM_EVENT_SERVICE_BLOCKED
} sample_stream_event_type_t;

typedef struct
{
    uint32_t sequence;
    uint32_t cause_sequence;
    uint64_t audio_frame;
    uint32_t cycle;
    sample_audio_key_t key;
    uint32_t page_index;
    uint32_t voice_generation;
    uint32_t value0;
    uint32_t value1;
    uint8_t type;
    uint8_t source;
    uint8_t voice_id;
    uint8_t result;
} sample_stream_event_t;

typedef struct
{
    uint32_t magic;
    uint32_t abi_version;
    uint32_t write_index;
    uint32_t count;
    uint32_t dropped_count;
    uint32_t last_miss_sequence;
    sample_stream_event_t events[SAMPLE_STREAM_EVENT_TRACE_CAPACITY];
} sample_stream_event_trace_snapshot_t;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(sample_stream_event_t) == 48U,
               "stream event ABI must remain fixed-width");
_Static_assert(sizeof(sample_stream_event_trace_snapshot_t) == 6168U,
               "stream event trace snapshot ABI must remain fixed-width");
#endif

extern volatile sample_stream_event_trace_snapshot_t g_sample_stream_event_trace;

void sample_stream_event_trace_reset(void);
uint32_t sample_stream_event_trace_record(sample_stream_event_type_t type,
                                           sample_audio_key_t key,
                                           uint32_t page_index,
                                           uint8_t source,
                                           uint8_t voice_id,
                                           uint32_t voice_generation,
                                           uint32_t cause_sequence,
                                           uint32_t value0,
                                           uint32_t value1,
                                           uint8_t result);
uint32_t sample_stream_event_trace_record_miss(sample_audio_key_t key,
                                               uint32_t page_index,
                                               uint32_t reader_position,
                                               uint32_t frames_remaining);

#ifdef __cplusplus
}
#endif
