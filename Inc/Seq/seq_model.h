#ifndef SEQ_MODEL_H
#define SEQ_MODEL_H

#include <stdint.h>

#include "Seq/seq_types.h"

typedef struct
{
    uint16_t next;
    seq_param8_t param8;
    uint8_t set_id;
    seq_value16_t value16;
    uint8_t flags;
    uint8_t reserved;
} seq_plock_entry_t;

typedef struct
{
    uint16_t lock_head;
    uint8_t lock_count;
    uint8_t trig;
    uint8_t lock_set_mask;
    uint8_t reserved[3];
} seq_step_t;

typedef struct
{
    seq_step_t steps[SEQ_MAX_STEPS];
    uint8_t length_steps;
    uint8_t ui_page;
    uint8_t reserved[2];
} seq_track_data_t;

typedef struct
{
    seq_track_data_t tracks[SEQ_TRACK_COUNT];
    seq_plock_entry_t pool[SEQ_TRACK_COUNT][SEQ_PLOCK_POOL_CAP_PER_TRACK];
    uint16_t free_head[SEQ_TRACK_COUNT];
    uint16_t free_count[SEQ_TRACK_COUNT];
} seq_project_data_t;

typedef enum
{
    SEQ_PLOCK_OP_INVALID = 0,
    SEQ_PLOCK_OP_CREATED,
    SEQ_PLOCK_OP_UPDATED,
    SEQ_PLOCK_OP_DELETED,
    SEQ_PLOCK_OP_NOT_FOUND,
    SEQ_PLOCK_OP_STEP_FULL,
    SEQ_PLOCK_OP_POOL_EMPTY,
    SEQ_PLOCK_OP_SET_NOT_PLOCKABLE
} seq_plock_op_status_t;

void seq_model_init_defaults(void);
const seq_project_data_t *seq_model_get_project(void);
uint8_t seq_model_load_project(const seq_project_data_t *project);

uint8_t seq_model_get_trig(seq_track_id_t track, seq_step_id_t step);
void seq_model_toggle_trig(seq_track_id_t track, seq_step_id_t step);
void seq_model_set_trig(seq_track_id_t track, seq_step_id_t step, uint8_t trig);
uint8_t seq_model_get_track_page(seq_track_id_t track);
void seq_model_set_track_page(seq_track_id_t track, uint8_t page);
void seq_model_set_track_length(seq_track_id_t track, uint8_t length_steps);
uint8_t seq_model_get_track_length(seq_track_id_t track);
uint8_t seq_model_get_editable_step_capacity(void);
uint8_t seq_model_is_step_editable_index(seq_step_id_t step);
uint8_t seq_model_get_track_playback_length(seq_track_id_t track);
uint8_t seq_model_is_step_in_track_playback_window(seq_track_id_t track, seq_step_id_t step);

uint8_t seq_model_step_plock_find(seq_track_id_t track,
                                  seq_step_id_t step,
                                  uint8_t set_id,
                                  seq_param8_t param8,
                                  seq_plock_entry_t *out_entry);
seq_plock_op_status_t seq_model_step_plock_upsert(seq_track_id_t track,
                                                   seq_step_id_t step,
                                                   uint8_t set_id,
                                                   seq_param8_t param8,
                                                   seq_value16_t value16,
                                                   uint8_t flags);
seq_plock_op_status_t seq_model_step_plock_delete(seq_track_id_t track,
                                                   seq_step_id_t step,
                                                   uint8_t set_id,
                                                   seq_param8_t param8);
void seq_model_step_plock_clear(seq_track_id_t track, seq_step_id_t step);
uint8_t seq_model_step_plock_count(seq_track_id_t track, seq_step_id_t step);
uint8_t seq_model_step_plock_get_at(seq_track_id_t track,
                                    seq_step_id_t step,
                                    uint8_t ordinal,
                                    seq_plock_entry_t *out_entry);

#endif /* SEQ_MODEL_H */
