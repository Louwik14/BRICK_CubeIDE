#ifndef SEQ_BOUNDARY_ENGINE_H
#define SEQ_BOUNDARY_ENGINE_H

#if !defined(SEQ_RUNTIME_INTERNAL_USE) && !defined(SEQ_BOUNDARY_ENGINE_IMPLEMENTATION)
#error "seq_boundary_engine is internal to the sequencer time-domain runtime."
#endif

#include <stdint.h>

#include "Seq/seq_runtime.h"

typedef struct
{
    seq_track_id_t track;
    seq_step_id_t step;
} seq_boundary_hit_t;

void seq_boundary_engine_restore_all_active_locks(seq_runtime_state_t *state,
                                                  seq_track_id_t track);
void seq_boundary_engine_advance_one_step(seq_runtime_state_t *state);
void seq_boundary_engine_process(seq_runtime_state_t *state,
                                 seq_boundary_hit_t *out_hits,
                                 uint8_t max_hits,
                                 uint8_t *out_hit_count);

#endif /* SEQ_BOUNDARY_ENGINE_H */
