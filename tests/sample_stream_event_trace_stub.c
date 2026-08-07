#include "Sampler/sample_stream_trace.h"

volatile sample_stream_event_trace_snapshot_t g_sample_stream_event_trace;

void sample_stream_event_trace_reset(void)
{
}

uint32_t sample_stream_event_trace_record(sample_stream_event_type_t type,
                                           sample_audio_key_t key,
                                           uint32_t page_index,
                                           uint8_t source,
                                           uint8_t voice_id,
                                           uint32_t voice_generation,
                                           uint32_t cause_sequence,
                                           uint32_t value0,
                                           uint32_t value1,
                                           uint8_t result)
{
    (void)type;
    (void)key;
    (void)page_index;
    (void)source;
    (void)voice_id;
    (void)voice_generation;
    (void)cause_sequence;
    (void)value0;
    (void)value1;
    (void)result;
    return 0U;
}

uint32_t sample_stream_event_trace_record_miss(sample_audio_key_t key,
                                               uint32_t page_index,
                                               uint32_t reader_position,
                                               uint32_t frames_remaining)
{
    (void)key;
    (void)page_index;
    (void)reader_position;
    (void)frames_remaining;
    return 0U;
}
