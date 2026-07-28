#ifndef SEQ_PLOCK_ROUTE_H
#define SEQ_PLOCK_ROUTE_H

#if !defined(SEQ_RUNTIME_INTERNAL_USE) && !defined(SEQ_BOUNDARY_ENGINE_IMPLEMENTATION) && !defined(SEQ_PLAY_SCHEDULER_IMPLEMENTATION) && !defined(SEQ_PLOCK_ROUTE_IMPLEMENTATION)
#error "seq_plock_route is internal to the sequencer time-domain runtime."
#endif

#include <stdint.h>

#include "Seq/seq_types.h"

#define SEQ_PLOCK_ROUTE_GROUP_MEMBER_MAX 8U

typedef struct
{
    seq_track_id_t scheduler_track;
    seq_step_id_t scheduler_step;
    seq_track_id_t source_track;
    seq_step_id_t source_step;
    uint8_t linked;
    uint8_t group_master;
    uint8_t target_count;
    seq_track_id_t targets[SEQ_PLOCK_ROUTE_GROUP_MEMBER_MAX];
} seq_plock_route_t;

uint8_t seq_plock_route_resolve(seq_track_id_t scheduler_track,
                                seq_step_id_t scheduler_step,
                                seq_plock_route_t *out_route);
uint8_t seq_plock_route_target_is_seq_link_slave(seq_track_id_t target_track);

#endif /* SEQ_PLOCK_ROUTE_H */
