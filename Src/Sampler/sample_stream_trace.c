#include "Sampler/sample_stream_trace.h"

#include <string.h>

#include "Sampler/sample_stream_time.h"
#include "stm32h7xx.h"

volatile sample_stream_event_trace_snapshot_t g_sample_stream_event_trace;

static uint32_t sample_stream_event_trace_find_cause(sample_audio_key_t key,
                                                      uint32_t page_index)
{
    uint32_t count = g_sample_stream_event_trace.count;
    if (count > SAMPLE_STREAM_EVENT_TRACE_CAPACITY)
    {
        count = SAMPLE_STREAM_EVENT_TRACE_CAPACITY;
    }

    uint32_t sequence = g_sample_stream_event_trace.write_index;
    for (uint32_t offset = 0U; offset < count; ++offset)
    {
        const uint32_t slot = (sequence - 1U) % SAMPLE_STREAM_EVENT_TRACE_CAPACITY;
        const sample_stream_event_t *const event =
            (const sample_stream_event_t *)&g_sample_stream_event_trace.events[slot];
        if ((event->sequence == sequence)
            && (event->key.domain == key.domain)
            && (event->key.object_id == key.object_id)
            && (event->page_index == page_index)
            && ((event->type == SAMPLE_STREAM_EVENT_SELECT)
                || (event->type == SAMPLE_STREAM_EVENT_LOAD_BEGIN)
                || (event->type == SAMPLE_STREAM_EVENT_LOAD_END)
                || (event->type == SAMPLE_STREAM_EVENT_READY)
                || (event->type == SAMPLE_STREAM_EVENT_LOAD_ERROR)))
        {
            return sequence;
        }
        sequence--;
    }
    return 0U;
}

static uint32_t sample_stream_event_trace_record_locked(
    sample_stream_event_type_t type,
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
    const uint32_t sequence = g_sample_stream_event_trace.write_index + 1U;
    const uint32_t slot = g_sample_stream_event_trace.write_index
                          % SAMPLE_STREAM_EVENT_TRACE_CAPACITY;
    sample_stream_event_t *const event =
        (sample_stream_event_t *)&g_sample_stream_event_trace.events[slot];
    *event = (sample_stream_event_t){
        .sequence = sequence,
        .cause_sequence = cause_sequence,
        .audio_frame = sample_stream_time_now(),
        .cycle = DWT->CYCCNT,
        .key = key,
        .page_index = page_index,
        .voice_generation = voice_generation,
        .value0 = value0,
        .value1 = value1,
        .type = (uint8_t)type,
        .source = source,
        .voice_id = voice_id,
        .result = result,
    };
    __DMB();
    g_sample_stream_event_trace.write_index = sequence;
    if (g_sample_stream_event_trace.count < SAMPLE_STREAM_EVENT_TRACE_CAPACITY)
    {
        g_sample_stream_event_trace.count++;
    }
    else
    {
        g_sample_stream_event_trace.dropped_count++;
    }
    if (type == SAMPLE_STREAM_EVENT_CONSUME_MISS)
    {
        g_sample_stream_event_trace.last_miss_sequence = sequence;
    }
    return sequence;
}

void sample_stream_event_trace_reset(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    memset((void *)&g_sample_stream_event_trace, 0, sizeof(g_sample_stream_event_trace));
    g_sample_stream_event_trace.magic = SAMPLE_STREAM_EVENT_TRACE_MAGIC;
    g_sample_stream_event_trace.abi_version = SAMPLE_STREAM_EVENT_TRACE_ABI_VERSION;
    if (primask == 0U)
    {
        __enable_irq();
    }
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
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t sequence = sample_stream_event_trace_record_locked(
        type,
        key,
        page_index,
        source,
        voice_id,
        voice_generation,
        cause_sequence,
        value0,
        value1,
        result);
    if (primask == 0U)
    {
        __enable_irq();
    }
    return sequence;
}

uint32_t sample_stream_event_trace_record_miss(sample_audio_key_t key,
                                               uint32_t page_index,
                                               uint32_t reader_position,
                                               uint32_t frames_remaining)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t cause_sequence = sample_stream_event_trace_find_cause(key, page_index);
    const uint32_t sequence = sample_stream_event_trace_record_locked(
        SAMPLE_STREAM_EVENT_CONSUME_MISS,
        key,
        page_index,
        0U,
        UINT8_MAX,
        0U,
        cause_sequence,
        reader_position,
        frames_remaining,
        1U);
    if (primask == 0U)
    {
        __enable_irq();
    }
    return sequence;
}
