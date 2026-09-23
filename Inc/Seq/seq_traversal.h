#ifndef SEQ_TRAVERSAL_H
#define SEQ_TRAVERSAL_H

#include <stdint.h>

typedef enum
{
    SEQ_DIRECTION_FWD = 0,
    SEQ_DIRECTION_REV,
    SEQ_DIRECTION_PINGPONG,
    SEQ_DIRECTION_RANDOM,
    SEQ_DIRECTION_COUNT
} seq_direction_t;

static inline uint8_t seq_traversal_cycle_length(uint8_t length,
                                                  uint8_t direction)
{
    if (length == 0U) length = 1U;
    if ((direction == (uint8_t)SEQ_DIRECTION_PINGPONG) && (length > 1U))
        return (uint8_t)(2U * length - 2U);
    return length;
}

static inline uint32_t seq_traversal_mix32(uint32_t value)
{
    value ^= value >> 16U;
    value *= UINT32_C(0x7FEB352D);
    value ^= value >> 15U;
    value *= UINT32_C(0x846CA68B);
    value ^= value >> 16U;
    return value;
}

static inline uint8_t seq_traversal_resolve(uint8_t phase,
                                             uint8_t length,
                                             uint8_t direction,
                                             int8_t rotate,
                                             uint32_t pattern_seed,
                                             uint8_t track)
{
    if (length == 0U) length = 1U;
    const uint8_t cycle = seq_traversal_cycle_length(length, direction);
    phase = (uint8_t)(phase % cycle);
    uint8_t directed = phase;
    if (direction == (uint8_t)SEQ_DIRECTION_REV)
        directed = (uint8_t)(length - 1U - phase);
    else if ((direction == (uint8_t)SEQ_DIRECTION_PINGPONG)
             && (phase >= length))
        directed = (uint8_t)(cycle - phase);
    else if (direction == (uint8_t)SEQ_DIRECTION_RANDOM)
    {
        const uint32_t identity = pattern_seed
            ^ ((uint32_t)(track + 1U) * UINT32_C(0x9E3779B9))
            ^ ((uint32_t)(phase + 1U) * UINT32_C(0x85EBCA6B));
        directed = (uint8_t)(seq_traversal_mix32(identity) % length);
    }
    int16_t stored = (int16_t)directed - (int16_t)rotate;
    stored %= (int16_t)length;
    if (stored < 0) stored += length;
    return (uint8_t)stored;
}

#endif /* SEQ_TRAVERSAL_H */
