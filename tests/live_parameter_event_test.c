#include "Core/live_parameter_event.h"
#include "Core/live_parameter_audio_queue.h"

#include <assert.h>

int main(void)
{
    assert(sizeof(live_parameter_audio_event_t) == 32U);
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

    return 0;
}
