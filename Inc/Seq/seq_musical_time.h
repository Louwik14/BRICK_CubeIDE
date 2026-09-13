#ifndef SEQ_MUSICAL_TIME_H
#define SEQ_MUSICAL_TIME_H

#include <stdint.h>

#include "Seq/seq_types.h"

/* CONTROL-owned projection of the TIM5/sample timeline into musical space.
 * Positions use Q16 steps.  They are coordinates, not independent clocks. */
typedef struct
{
    uint64_t sample_time;
    uint64_t transport_position_q16;
    uint32_t pattern_position_q16;
    uint32_t loop_epoch;
    uint32_t samples_per_step_q16;
    uint8_t track_div;
    uint8_t running;
    uint8_t reserved[2];
} seq_musical_time_t;

_Static_assert(sizeof(seq_musical_time_t) == 32U,
               "musical time projection must remain compact");

uint8_t seq_runtime_get_musical_time(seq_track_id_t track,
                                     uint64_t sample_time,
                                     seq_musical_time_t *out_time);

#endif
