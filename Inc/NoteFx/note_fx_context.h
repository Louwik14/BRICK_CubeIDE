#ifndef NOTE_FX_CONTEXT_H
#define NOTE_FX_CONTEXT_H

#include <stdint.h>

#include "Seq/seq_musical_time.h"

typedef enum
{
    NOTE_FX_PHASE_TRANSPORT_FREE = 0,
    NOTE_FX_PHASE_PATTERN_SYNC
} note_fx_phase_policy_t;

/* Immutable input prepared by CONTROL for one NoteFx evaluation.  PASS 2 will
 * route this context through temporal engines; it never owns or advances time. */
typedef struct
{
    seq_musical_time_t time;
    uint8_t track;
    uint8_t reserved[3];
} note_fx_processing_context_t;

static inline uint64_t note_fx_context_phase_position_q16(
    const note_fx_processing_context_t *context,
    note_fx_phase_policy_t policy)
{
    if (context == 0) return 0U;
    return (policy == NOTE_FX_PHASE_PATTERN_SYNC)
        ? context->time.pattern_position_q16
        : context->time.transport_position_q16;
}

#endif
