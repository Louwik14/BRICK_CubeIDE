#ifndef SEQ_STEP_SNAPSHOT_H
#define SEQ_STEP_SNAPSHOT_H

#include <stdint.h>

#include "Seq/seq_model.h"

#define SEQ_STEP_SNAPSHOT_MAX_STEPS SEQ_MAX_STEPS
#define SEQ_STEP_SNAPSHOT_MAX_LOCKS SEQ_STEP_MAX_LOCKS

typedef struct
{
    uint8_t set_id;
    seq_param_slot_t param_slot;
    seq_value16_t value16;
    uint8_t flags;
} seq_step_snapshot_plock_t;

typedef struct
{
    uint8_t valid;
    uint8_t trig;
    uint8_t roll;
    uint8_t lock_count;
    uint8_t reserved[4];
    seq_play_snapshot_t play;
    seq_step_snapshot_plock_t locks[SEQ_STEP_SNAPSHOT_MAX_LOCKS];
} seq_step_snapshot_t;

_Static_assert(sizeof(seq_step_snapshot_t) == 240U, "step snapshot storage size changed");

typedef struct
{
    seq_step_id_t step;
    seq_step_snapshot_t snapshot;
} seq_step_snapshot_entry_t;

typedef struct
{
    uint8_t count;
    uint8_t reserved[3];
    seq_step_snapshot_entry_t entries[SEQ_STEP_SNAPSHOT_MAX_STEPS];
} seq_step_snapshot_list_t;

uint8_t seq_step_snapshot_capture(seq_track_id_t track,
                                   seq_step_id_t step,
                                   seq_step_snapshot_t *out_snapshot);

uint8_t seq_step_snapshot_capture_list(seq_track_id_t track,
                                        const seq_step_id_t *steps,
                                        uint8_t step_count,
                                        seq_step_snapshot_list_t *out_list);

uint8_t seq_step_snapshot_validate_for_track(seq_track_id_t track,
                                              const seq_step_snapshot_t *snapshot);
uint8_t seq_step_snapshot_project_play_for_track(seq_track_id_t track,
                                                  seq_step_snapshot_t *snapshot);
uint8_t seq_step_snapshot_validate_for_target(uint8_t can_store_play,
                                              uint8_t can_store_params,
                                              uint8_t runtime_type,
                                              const seq_step_snapshot_t *snapshot);

uint8_t seq_step_snapshot_apply(seq_track_id_t track,
                                seq_step_id_t step,
                                const seq_step_snapshot_t *snapshot);

uint8_t seq_step_snapshot_can_apply_list(seq_track_id_t track,
                                         const seq_step_snapshot_list_t *list);

uint8_t seq_step_snapshot_apply_list(seq_track_id_t track,
                                     const seq_step_snapshot_list_t *list);

uint8_t seq_step_snapshot_equal(const seq_step_snapshot_t *left,
                                const seq_step_snapshot_t *right);

#endif /* SEQ_STEP_SNAPSHOT_H */
