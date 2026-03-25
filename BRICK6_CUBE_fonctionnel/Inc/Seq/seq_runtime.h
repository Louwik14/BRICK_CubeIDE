#ifndef SEQ_RUNTIME_H
#define SEQ_RUNTIME_H

#include <stdint.h>

#include "Seq/seq_types.h"

typedef struct
{
    uint8_t active;
    uint8_t set_id;
    seq_param8_t param8;
    uint8_t reserved;
    seq_value16_t base_value16;
} seq_runtime_active_lock_t;

typedef struct
{
    uint8_t running;
    seq_clock_src_t clock_src;
    uint8_t play_step[SEQ_TRACK_COUNT];
    uint8_t prev_step[SEQ_TRACK_COUNT];
    uint8_t prev_step_valid[SEQ_TRACK_COUNT];
    uint8_t active_lock_count[SEQ_TRACK_COUNT];
    uint32_t last_tick_count;
    uint32_t tick_accum;
    uint16_t ticks_per_step;
    uint8_t save_pending;
    uint8_t reserved;
    uint32_t save_retry_tick;
    seq_runtime_active_lock_t active_locks[SEQ_TRACK_COUNT][SEQ_STEP_MAX_LOCKS];
} seq_runtime_state_t;

void seq_runtime_init(void);
void seq_runtime_process(void);
const seq_runtime_state_t *seq_runtime_get_state(void);

void seq_runtime_start(void);
void seq_runtime_stop(void);
void seq_runtime_toggle_play_stop(void);
uint8_t seq_runtime_is_running(void);

uint8_t seq_runtime_set_playhead_step(seq_track_id_t track, seq_step_id_t step);
uint8_t seq_runtime_get_playhead_step(seq_track_id_t track, seq_step_id_t *out_step);

#endif /* SEQ_RUNTIME_H */
