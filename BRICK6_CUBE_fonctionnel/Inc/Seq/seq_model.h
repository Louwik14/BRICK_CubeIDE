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
    seq_plock_entry_t pool[SEQ_PLOCK_POOL_CAP];
    uint16_t free_head;
    uint16_t free_count;
} seq_project_data_t;


void seq_model_init_defaults(void);
const seq_project_data_t *seq_model_get_project(void);

uint8_t seq_model_get_trig(seq_track_id_t track, seq_step_id_t step);
void seq_model_toggle_trig(seq_track_id_t track, seq_step_id_t step);
uint8_t seq_model_get_track_page(seq_track_id_t track);
void seq_model_set_track_page(seq_track_id_t track, uint8_t page);

#endif /* SEQ_MODEL_H */
