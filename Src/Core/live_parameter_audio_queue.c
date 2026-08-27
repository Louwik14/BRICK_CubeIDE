#include "Core/live_parameter_audio_queue.h"

#include "Core/live_clock.h"
#include "Core/project_control.h"
#include "Core/live_parameter_event.h"
#include "memory_layout.h"
#include "Seq/seq_runtime_exec.h"
#include "stm32h7xx_hal.h"

typedef struct
{
    live_parameter_audio_event_t events[LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
} live_parameter_audio_queue_state_t;

typedef struct
{
    live_parameter_audio_dated_event_t events[
        LIVE_PARAMETER_AUDIO_DATED_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
} live_parameter_audio_dated_queue_state_t;

D3_IPC static live_parameter_audio_queue_state_t g_live_parameter_audio_queue;
D3_IPC static live_parameter_audio_dated_queue_state_t
    g_live_parameter_audio_dated_queue;
static uint32_t g_live_parameter_audio_bulk_serial;
static uint32_t g_live_parameter_audio_publish_failure_count;

static bool live_parameter_audio_publish_failed(void)
{
    ++g_live_parameter_audio_publish_failure_count;
    return false;
}

static uint8_t live_parameter_audio_project_sampler_value(
    uint16_t parameter_id,
    uint8_t track,
    float logical_value,
    float *out_runtime_value)
{
    if (parameter_id != (uint16_t)PARAM_SAMPLER_SAMPLE)
    {
        if (out_runtime_value != NULL)
            *out_runtime_value = logical_value;
        return 1U;
    }
    return project_control_resolve_audio_sampler_value(
        track, logical_value, out_runtime_value);
}

static uint8_t live_parameter_audio_schedule_bulk(
    const live_parameter_audio_event_t *events,
    uint8_t count)
{
    if ((events == 0) || (count == 0U)
            || (count > LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS))
    {
        return 0U;
    }

    uint32_t head = g_live_parameter_audio_queue.head;
    const uint32_t tail = g_live_parameter_audio_queue.tail;
    __DMB();
    if ((head - tail + count) > LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY)
        return 0U;

    uint64_t publication_floor = 0U;
    if (head != tail)
        publication_floor = g_live_parameter_audio_queue.events[
            (head - 1U) & (LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY - 1U)]
                .effective_sample_time;
    for (uint8_t item = 0U; item < count; ++item)
    {
        live_parameter_audio_event_t published = events[item];
        if (published.effective_sample_time < publication_floor)
            published.effective_sample_time = publication_floor;
        publication_floor = published.effective_sample_time;
        g_live_parameter_audio_queue.events[
            head & (LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY - 1U)] = published;
        ++head;
    }
    __DMB();
    g_live_parameter_audio_queue.head = head;
    return 1U;
}

static uint8_t live_parameter_audio_convert_capture(uint32_t capture_tick,
                                                    uint64_t *out_effective_sample_time)
{
    if (live_clock_tim5_to_guarded_sample_time(capture_tick,
                                               out_effective_sample_time) == 0U)
    {
        return 0U;
    }

    const uint64_t now = seq_runtime_exec_get_sample_timeline();
    if (*out_effective_sample_time < now)
    {
        *out_effective_sample_time = now;
    }
    return 1U;
}

void live_parameter_audio_queue_init(void)
{
    g_live_parameter_audio_queue.head = 0U;
    g_live_parameter_audio_queue.tail = 0U;
    g_live_parameter_audio_dated_queue.head = 0U;
    g_live_parameter_audio_dated_queue.tail = 0U;
    g_live_parameter_audio_bulk_serial = 0U;
    g_live_parameter_audio_publish_failure_count = 0U;
    __DMB();
}

uint16_t live_parameter_audio_queue_drain(void)
{
    uint16_t drained = 0U;

    for (uint16_t i = 0U; i < LIVE_PARAMETER_AUDIO_DRAIN_BUDGET; ++i)
    {
        live_parameter_event_t control_event;
        if (live_parameter_event_peek(&control_event) == 0U)
            break;

        uint64_t effective_sample_time = 0U;
        if (live_parameter_audio_convert_capture(control_event.capture_tick,
                                                 &effective_sample_time) == 0U)
            break;

        live_parameter_audio_event_t audio_event = {
            .effective_sample_time = effective_sample_time,
            .capture_tick = control_event.capture_tick,
            .ingress_serial = control_event.ingress_serial,
            .parameter_id = control_event.parameter_id,
            .source = control_event.source,
            .scope = control_event.scope,
            .track = control_event.track,
            .slot = control_event.slot,
            .flags = control_event.flags,
            .value = control_event.value,
            .matrix_operation = LIVE_PARAMETER_MATRIX_OPERATION_NONE
        };
        if ((control_event.scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
                && ((control_event.flags & LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS) != 0U))
        {
            float runtime_value = 0.0f;
            const float logical_value =
                live_parameter_event_decode_float(control_event.value);
            if (live_parameter_audio_project_sampler_value(
                    control_event.parameter_id, control_event.track,
                    logical_value, &runtime_value) == 0U)
                break;
            audio_event.value = live_parameter_event_encode_float(runtime_value);
        }
        if (live_parameter_audio_schedule_bulk(&audio_event, 1U) == 0U)
            break;
        live_parameter_event_consume();
        ++drained;
    }

    return drained;
}

bool live_parameter_audio_queue_submit_bulk(const live_parameter_audio_bulk_t *bulk)
{
    if ((bulk == 0) || (bulk->count == 0U)
            || (bulk->count > LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS))
    {
        return live_parameter_audio_publish_failed();
    }

    uint64_t effective_sample_time = 0U;
    if (live_parameter_audio_convert_capture(bulk->capture_tick,
                                             &effective_sample_time) == 0U)
    {
        return live_parameter_audio_publish_failed();
    }

    live_parameter_audio_event_t events[LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS];
    const uint32_t serial_base = 0x80000000U
                               | g_live_parameter_audio_bulk_serial;
    g_live_parameter_audio_bulk_serial += LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS;
    for (uint8_t i = 0U; i < bulk->count; ++i)
    {
        const live_parameter_audio_bulk_item_t *const item = &bulk->item[i];
        if (item->parameter_id >= PARAM_COUNT)
        {
            return live_parameter_audio_publish_failed();
        }

        for (uint8_t previous = 0U; previous < i; ++previous)
        {
            if ((events[previous].parameter_id == item->parameter_id)
                    && (events[previous].scope == item->scope)
                    && (events[previous].track == item->track)
                    && (events[previous].slot == item->slot))
            {
                return live_parameter_audio_publish_failed();
            }
        }

        events[i] = (live_parameter_audio_event_t){
            .effective_sample_time = effective_sample_time,
            .capture_tick = bulk->capture_tick,
            .ingress_serial = serial_base + i,
            .parameter_id = item->parameter_id,
            .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
            .scope = item->scope,
            .track = item->track,
            .slot = item->slot,
            .flags = live_parameter_event_bulk_flags(item->flags, i, bulk->count),
            .value = item->value,
            .matrix_operation = item->reserved
        };
        if ((item->scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
                && ((item->flags & LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS) != 0U))
        {
            float runtime_value = 0.0f;
            const float logical_value =
                live_parameter_event_decode_float(item->value);
            if (live_parameter_audio_project_sampler_value(
                    item->parameter_id, item->track,
                    logical_value, &runtime_value) == 0U)
                return live_parameter_audio_publish_failed();
            events[i].value = live_parameter_event_encode_float(runtime_value);
        }
    }

    if (live_parameter_audio_schedule_bulk(events, bulk->count) == 0U)
        return live_parameter_audio_publish_failed();
    return true;
}

bool live_parameter_audio_queue_submit_poly_pair(uint32_t capture_tick,
                                                 uint8_t track,
                                                 float voices,
                                                 float spread)
{
    uint64_t effective_sample_time = 0U;
    if ((track >= SEQ_LANE_CAPACITY)
            || (live_parameter_audio_convert_capture(
                    capture_tick, &effective_sample_time) == 0U))
    {
        return live_parameter_audio_publish_failed();
    }

    const uint32_t serial_base = 0x80000000U
                               | g_live_parameter_audio_bulk_serial;
    g_live_parameter_audio_bulk_serial += 2U;

    live_parameter_audio_event_t events[2] = {
        {
            .effective_sample_time = effective_sample_time,
            .capture_tick = capture_tick,
            .ingress_serial = serial_base,
            .parameter_id = PARAM_CFG_POLY_VOICES,
            .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = live_parameter_event_bulk_flags(
                (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                           | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS), 0U, 2U),
            .value = live_parameter_event_encode_float(voices)
        },
        {
            .effective_sample_time = effective_sample_time,
            .capture_tick = capture_tick,
            .ingress_serial = serial_base + 1U,
            .parameter_id = PARAM_CFG_POLY_SPREAD,
            .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = live_parameter_event_bulk_flags(
                (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                           | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS), 1U, 2U),
            .value = live_parameter_event_encode_float(spread)
        }
    };
    if (live_parameter_audio_schedule_bulk(events, 2U) == 0U)
        return live_parameter_audio_publish_failed();
    return true;
}

bool live_parameter_audio_queue_submit_dated(uint64_t effective_sample_time,
                                             uint16_t parameter_id,
                                             uint8_t track,
                                             uint16_t value16,
                                             uint8_t matrix_operation)
{
    if ((parameter_id >= PARAM_COUNT) || (track >= SEQ_LANE_CAPACITY))
        return live_parameter_audio_publish_failed();

    const uint32_t head = g_live_parameter_audio_dated_queue.head;
    const uint32_t tail = g_live_parameter_audio_dated_queue.tail;
    __DMB();
    if ((head - tail) >= LIVE_PARAMETER_AUDIO_DATED_CAPACITY)
        return live_parameter_audio_publish_failed();
    uint16_t projected_value16 = value16;
    if (parameter_id == (uint16_t)PARAM_SAMPLER_SAMPLE)
    {
        float runtime_value = 0.0f;
        if (live_parameter_audio_project_sampler_value(
                parameter_id, track, (float)value16, &runtime_value) == 0U)
            return live_parameter_audio_publish_failed();
        projected_value16 = (uint16_t)(runtime_value + 0.5f);
    }
    g_live_parameter_audio_dated_queue.events[
        head & (LIVE_PARAMETER_AUDIO_DATED_CAPACITY - 1U)] =
        (live_parameter_audio_dated_event_t){
        .due_sample_low = (uint16_t)effective_sample_time,
        .parameter_id = parameter_id,
        .track = track,
        .value16 = projected_value16,
        .matrix_operation = matrix_operation
    };
    __DMB();
    g_live_parameter_audio_dated_queue.head = head + 1U;
    return true;
}

uint32_t live_parameter_audio_queue_publish_failure_count(void)
{
    return g_live_parameter_audio_publish_failure_count;
}

uint16_t live_parameter_audio_queue_frames_until_deadline(uint64_t block_start,
                                                          uint16_t max_frames)
{
    const uint32_t tail = g_live_parameter_audio_queue.tail;
    __DMB();
    if ((tail != g_live_parameter_audio_queue.head) && (max_frames != 0U))
    {
        const uint64_t deadline = g_live_parameter_audio_queue.events[
            tail & (LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY - 1U)]
                .effective_sample_time;
        if ((deadline > block_start)
                && ((deadline - block_start) < (uint64_t)max_frames))
            max_frames = (uint16_t)(deadline - block_start);
    }
    const uint32_t dated_tail = g_live_parameter_audio_dated_queue.tail;
    __DMB();
    if ((dated_tail != g_live_parameter_audio_dated_queue.head)
            && (max_frames != 0U))
    {
        const uint16_t deadline = g_live_parameter_audio_dated_queue.events[
            dated_tail & (LIVE_PARAMETER_AUDIO_DATED_CAPACITY - 1U)]
                .due_sample_low;
        const int16_t distance = (int16_t)(deadline - (uint16_t)block_start);
        if ((distance > 0) && ((uint16_t)distance < max_frames))
            max_frames = (uint16_t)distance;
    }
    return max_frames;
}

bool live_parameter_audio_queue_audio_peek(live_parameter_audio_event_t *out_event)
{
    if (out_event == 0)
        return false;
    const uint32_t tail = g_live_parameter_audio_queue.tail;
    if (tail == g_live_parameter_audio_queue.head)
        return false;
    __DMB();
    *out_event = g_live_parameter_audio_queue.events[
        tail & (LIVE_PARAMETER_AUDIO_QUEUE_CAPACITY - 1U)];
    return true;
}

bool live_parameter_audio_queue_audio_pop(void)
{
    const uint32_t tail = g_live_parameter_audio_queue.tail;
    if (tail == g_live_parameter_audio_queue.head)
        return false;
    __DMB();
    g_live_parameter_audio_queue.tail = tail + 1U;
    __DMB();
    return true;
}

bool live_parameter_audio_queue_audio_peek_dated(
    live_parameter_audio_dated_event_t *out_event)
{
    if (out_event == 0)
        return false;
    const uint32_t tail = g_live_parameter_audio_dated_queue.tail;
    if (tail == g_live_parameter_audio_dated_queue.head)
        return false;
    __DMB();
    *out_event = g_live_parameter_audio_dated_queue.events[
        tail & (LIVE_PARAMETER_AUDIO_DATED_CAPACITY - 1U)];
    return true;
}

bool live_parameter_audio_queue_audio_pop_dated(void)
{
    const uint32_t tail = g_live_parameter_audio_dated_queue.tail;
    if (tail == g_live_parameter_audio_dated_queue.head)
        return false;
    __DMB();
    g_live_parameter_audio_dated_queue.tail = tail + 1U;
    __DMB();
    return true;
}
