#ifndef BRICK6_LIVE_PARAMETER_EVENT_H
#define BRICK6_LIVE_PARAMETER_EVENT_H

#include <stdint.h>

typedef enum
{
    LIVE_PARAMETER_EVENT_SCOPE_GLOBAL = 0U,
    LIVE_PARAMETER_EVENT_SCOPE_TRACK = 1U,
    LIVE_PARAMETER_EVENT_SCOPE_SLOT = 2U
} live_parameter_event_scope_t;

#define LIVE_PARAMETER_EVENT_INVALID_INDEX 0xFFU
static inline int32_t live_parameter_event_encode_float(float value)
{
    union
    {
        float f;
        int32_t i;
    } bits = { .f = value };

    return bits.i;
}

static inline float live_parameter_event_decode_float(int32_t value)
{
    union
    {
        float f;
        int32_t i;
    } bits = { .i = value };

    return bits.f;
}

#endif /* BRICK6_LIVE_PARAMETER_EVENT_H */
