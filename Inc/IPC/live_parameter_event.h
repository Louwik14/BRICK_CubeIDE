#ifndef BRICK6_LIVE_PARAMETER_EVENT_H
#define BRICK6_LIVE_PARAMETER_EVENT_H

#include <stdint.h>

typedef enum
{
    LIVE_PARAMETER_EVENT_SCOPE_GLOBAL = 0U,
    LIVE_PARAMETER_EVENT_SCOPE_TRACK = 1U,
    LIVE_PARAMETER_EVENT_SCOPE_SLOT = 2U
} live_parameter_event_scope_t;

/* PARAM command kind values 2..9 address one canonical Matrix slot while
 * preserving the public PARAM opcode and the 16-byte command ABI. */
#define LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE 2U
#define LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_LAST 9U
#define LIVE_PARAMETER_AUDIO_SCOPE_RUNTIME_TEMP     10U

enum
{
    LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS = (uint16_t)(1U << 1),
    /* Temporary effective target: apply in audio without replacing the
     * control/UI base value (macro, modulation-like override). */
    LIVE_PARAMETER_EVENT_FLAG_RUNTIME_TEMP = (uint16_t)(1U << 15)
};

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
