#ifndef BRICK6_LIVE_PARAMETER_EVENT_H
#define BRICK6_LIVE_PARAMETER_EVENT_H

#include <stdbool.h>
#include <stdint.h>

#include "param_store.h"

/* The value field carries the exact IEEE-754 bits of the canonical float
 * value.  The wire format stays integer-only and pointer-free. */
typedef struct
{
    uint32_t capture_tick;
    uint32_t ingress_serial;
    uint16_t parameter_id;
    uint8_t source;
    uint8_t scope;
    uint8_t track;
    uint8_t slot;
    uint16_t flags;
    int32_t value;
} live_parameter_event_t;

_Static_assert(sizeof(live_parameter_event_t) == 20U,
               "live_parameter_event_t must remain a fixed 20-byte event");

typedef enum
{
    LIVE_PARAMETER_EVENT_SOURCE_ENCODER = 0U,
    LIVE_PARAMETER_EVENT_SOURCE_BULK = 1U
} live_parameter_event_source_t;

typedef enum
{
    LIVE_PARAMETER_EVENT_SCOPE_GLOBAL = 0U,
    LIVE_PARAMETER_EVENT_SCOPE_TRACK = 1U,
    LIVE_PARAMETER_EVENT_SCOPE_SLOT = 2U
} live_parameter_event_scope_t;

enum
{
    LIVE_PARAMETER_EVENT_FLAG_SET_TARGET = (uint16_t)(1U << 0),
    LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS = (uint16_t)(1U << 1),
    LIVE_PARAMETER_EVENT_FLAG_BULK = (uint16_t)(1U << 2),
    LIVE_PARAMETER_EVENT_FLAG_BULK_INDEX_SHIFT = 3U,
    LIVE_PARAMETER_EVENT_FLAG_BULK_INDEX_MASK = (uint16_t)(0x3FU << LIVE_PARAMETER_EVENT_FLAG_BULK_INDEX_SHIFT),
    LIVE_PARAMETER_EVENT_FLAG_BULK_COUNT_SHIFT = 9U,
    LIVE_PARAMETER_EVENT_FLAG_BULK_COUNT_MASK = (uint16_t)(0x3FU << LIVE_PARAMETER_EVENT_FLAG_BULK_COUNT_SHIFT),
    LIVE_PARAMETER_EVENT_FLAG_ENCODER_SHIFT = 8U,
    LIVE_PARAMETER_EVENT_FLAG_ENCODER_MASK = (uint16_t)(0x03U << LIVE_PARAMETER_EVENT_FLAG_ENCODER_SHIFT)
};

static inline uint16_t live_parameter_event_bulk_flags(uint16_t flags,
                                                       uint8_t index,
                                                       uint8_t count)
{
    flags = (uint16_t)(flags | LIVE_PARAMETER_EVENT_FLAG_BULK);
    flags = (uint16_t)(flags & (uint16_t)~(LIVE_PARAMETER_EVENT_FLAG_BULK_INDEX_MASK
                                           | LIVE_PARAMETER_EVENT_FLAG_BULK_COUNT_MASK));
    flags = (uint16_t)(flags | ((uint16_t)(index & 0x3FU)
                                << LIVE_PARAMETER_EVENT_FLAG_BULK_INDEX_SHIFT));
    const uint8_t encoded_count = (count == 64U) ? 0U : count;
    flags = (uint16_t)(flags | ((uint16_t)(encoded_count & 0x3FU)
                                << LIVE_PARAMETER_EVENT_FLAG_BULK_COUNT_SHIFT));
    return flags;
}

static inline uint8_t live_parameter_event_bulk_index(uint16_t flags)
{
    return (uint8_t)((flags & LIVE_PARAMETER_EVENT_FLAG_BULK_INDEX_MASK)
                     >> LIVE_PARAMETER_EVENT_FLAG_BULK_INDEX_SHIFT);
}

static inline uint8_t live_parameter_event_bulk_count(uint16_t flags)
{
    const uint8_t encoded_count = (uint8_t)((flags & LIVE_PARAMETER_EVENT_FLAG_BULK_COUNT_MASK)
                                            >> LIVE_PARAMETER_EVENT_FLAG_BULK_COUNT_SHIFT);
    return (encoded_count == 0U) ? 64U : encoded_count;
}

#define LIVE_PARAMETER_EVENT_INVALID_INDEX 0xFFU
#define LIVE_PARAMETER_EVENT_QUEUE_CAPACITY 64U

_Static_assert((LIVE_PARAMETER_EVENT_QUEUE_CAPACITY
                & (LIVE_PARAMETER_EVENT_QUEUE_CAPACITY - 1U)) == 0U,
               "live parameter event queue capacity must be a power of two");

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

void live_parameter_event_init(void);
bool live_parameter_event_submit(const live_parameter_event_t *event);
bool live_parameter_event_pop(live_parameter_event_t *out_event);
uint16_t live_parameter_event_depth(void);
uint32_t live_parameter_event_drop_count(void);

#endif /* BRICK6_LIVE_PARAMETER_EVENT_H */
