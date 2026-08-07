#pragma once

#include <stdint.h>

#include "Sampler/sample_stream_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t current_frame;
    uint32_t region_begin;
    uint32_t region_end;
    uint32_t loop_begin;
    uint32_t loop_end;
    uint32_t frames_per_page;
    int8_t direction;
    uint8_t loop_enabled;
} sample_stream_sequence_input_t;

/*
 * Produces the first occurrence of each physical page in playback order.
 * Duplicate pages caused by a short loop are intentionally suppressed: the
 * caller needs a residence set, not an unbounded playback trace.
 */
uint8_t sample_stream_sequence_build(const sample_stream_sequence_input_t *input,
                                     uint32_t *out_page_indices,
                                     uint8_t page_capacity,
                                     uint8_t *out_page_count);

#ifdef __cplusplus
}
#endif
