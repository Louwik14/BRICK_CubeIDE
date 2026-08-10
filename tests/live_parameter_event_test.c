#include "Core/live_parameter_event.h"
#include "Core/live_parameter_audio_queue.h"

#include <assert.h>

static live_parameter_event_t make_event(uint32_t serial)
{
    return (live_parameter_event_t){
        .capture_tick = serial + 1000U,
        .ingress_serial = serial,
        .parameter_id = 42U,
        .source = LIVE_PARAMETER_EVENT_SOURCE_ENCODER,
        .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
        .track = 3U,
        .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                            | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
        .value = live_parameter_event_encode_float((float)serial)
    };
}

static void test_queue_wrap_stress(void)
{
    live_parameter_event_t out;

    live_parameter_event_init();
    for (uint32_t serial = 1U; serial <= 100000U; ++serial)
    {
        const live_parameter_event_t event = make_event(serial);
        assert(live_parameter_event_submit(&event));
        assert(live_parameter_event_depth() == 1U);
        assert(live_parameter_event_pop(&out));
        assert(out.ingress_serial == serial);
        assert(out.capture_tick == serial + 1000U);
        assert(live_parameter_event_decode_float(out.value) == (float)serial);
        assert(live_parameter_event_depth() == 0U);
    }
    assert(live_parameter_event_drop_count() == 0U);
}

static void test_queue_saturation_and_reuse(void)
{
    live_parameter_event_t out;

    live_parameter_event_init();
    for (uint32_t serial = 1U; serial <= LIVE_PARAMETER_EVENT_QUEUE_CAPACITY; ++serial)
    {
        const live_parameter_event_t event = make_event(serial);
        assert(live_parameter_event_submit(&event));
    }
    assert(live_parameter_event_depth() == LIVE_PARAMETER_EVENT_QUEUE_CAPACITY);
    const live_parameter_event_t rejected = make_event(999U);
    assert(!live_parameter_event_submit(&rejected));
    assert(live_parameter_event_drop_count() == 1U);

    for (uint32_t serial = 1U; serial <= LIVE_PARAMETER_EVENT_QUEUE_CAPACITY; ++serial)
    {
        assert(live_parameter_event_pop(&out));
        assert(out.ingress_serial == serial);
    }
    assert(!live_parameter_event_pop(&out));

    for (uint32_t serial = 10001U; serial <= 10128U; ++serial)
    {
        const live_parameter_event_t event = make_event(serial);
        assert(live_parameter_event_submit(&event));
        assert(live_parameter_event_pop(&out));
        assert(out.ingress_serial == serial);
    }
}

int main(void)
{
    assert(sizeof(live_parameter_audio_event_t) == 32U);
    assert((LIVE_PARAMETER_EVENT_FLAG_RUNTIME_TEMP
            & (LIVE_PARAMETER_EVENT_FLAG_BULK_INDEX_MASK
               | LIVE_PARAMETER_EVENT_FLAG_BULK_COUNT_MASK)) == 0U);
    const float values[] = { -24.0f, -0.01f, 0.0f, 0.5f, 127.0f };

    for (unsigned int i = 0U; i < (sizeof(values) / sizeof(values[0])); ++i)
    {
        const int32_t encoded = live_parameter_event_encode_float(values[i]);
        assert(live_parameter_event_decode_float(encoded) == values[i]);
    }

    const live_parameter_event_t event = {
        .capture_tick = 1234U,
        .ingress_serial = 7U,
        .parameter_id = 42U,
        .source = LIVE_PARAMETER_EVENT_SOURCE_ENCODER,
        .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
        .track = 3U,
        .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                            | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS
                            | (2U << LIVE_PARAMETER_EVENT_FLAG_ENCODER_SHIFT)),
        .value = live_parameter_event_encode_float(0.75f)
    };

    assert(event.capture_tick == 1234U);
    assert(event.ingress_serial == 7U);
    assert(event.parameter_id == 42U);
    assert(event.scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK);
    assert(event.track == 3U);
    assert(live_parameter_event_decode_float(event.value) == 0.75f);
    assert((event.flags & LIVE_PARAMETER_EVENT_FLAG_SET_TARGET) != 0U);
    assert((event.flags & LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS) != 0U);
    assert(((event.flags & LIVE_PARAMETER_EVENT_FLAG_ENCODER_MASK)
            >> LIVE_PARAMETER_EVENT_FLAG_ENCODER_SHIFT) == 2U);

    test_queue_wrap_stress();
    test_queue_saturation_and_reuse();

    return 0;
}
