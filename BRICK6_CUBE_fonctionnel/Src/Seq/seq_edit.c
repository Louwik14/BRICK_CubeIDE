#include "Seq/seq_edit.h"

#include "Seq/seq_model.h"

void seq_edit_init(void)
{
    /* Step-2: editor has no private state yet. */
}

uint8_t seq_edit_toggle_hall_step(seq_track_id_t track, uint8_t hall_index)
{
    seq_step_id_t step = 0U;
    if (seq_edit_map_hall_to_step(track, hall_index, &step) == 0U)
    {
        return 0U;
    }

    seq_model_toggle_trig(track, step);
    return 1U;
}

void seq_edit_change_page(seq_track_id_t track, int8_t delta)
{
    uint8_t page = seq_model_get_track_page(track);

    if (delta > 0)
    {
        if (page < (SEQ_PAGE_COUNT - 1U))
        {
            page++;
        }
    }
    else if (delta < 0)
    {
        if (page > 0U)
        {
            page--;
        }
    }

    seq_model_set_track_page(track, page);
}

uint8_t seq_edit_get_page(seq_track_id_t track)
{
    return seq_model_get_track_page(track);
}

uint8_t seq_edit_map_hall_to_step(seq_track_id_t track, uint8_t hall_index, seq_step_id_t *out_step)
{
    (void)track;
    if (hall_index >= SEQ_STEPS_PER_PAGE)
    {
        return 0U;
    }

    const uint8_t page = seq_model_get_track_page(track);
    const uint8_t step = (uint8_t)(page * SEQ_STEPS_PER_PAGE + hall_index);

    if (step >= SEQ_MAX_STEPS)
    {
        return 0U;
    }

    if (out_step != 0)
    {
        *out_step = step;
    }

    return 1U;
}

uint8_t seq_edit_step_plock_find(seq_track_id_t track,
                                 seq_step_id_t step,
                                 uint8_t set_id,
                                 seq_param8_t param8,
                                 seq_plock_entry_t *out_entry)
{
    return seq_model_step_plock_find(track, step, set_id, param8, out_entry);
}

seq_plock_op_status_t seq_edit_step_plock_upsert(seq_track_id_t track,
                                                  seq_step_id_t step,
                                                  uint8_t set_id,
                                                  seq_param8_t param8,
                                                  seq_value16_t value16,
                                                  uint8_t flags)
{
    return seq_model_step_plock_upsert(track, step, set_id, param8, value16, flags);
}

seq_plock_op_status_t seq_edit_step_plock_delete(seq_track_id_t track,
                                                  seq_step_id_t step,
                                                  uint8_t set_id,
                                                  seq_param8_t param8)
{
    return seq_model_step_plock_delete(track, step, set_id, param8);
}

void seq_edit_step_plock_clear(seq_track_id_t track, seq_step_id_t step)
{
    seq_model_step_plock_clear(track, step);
}

uint8_t seq_edit_step_plock_count(seq_track_id_t track, seq_step_id_t step)
{
    return seq_model_step_plock_count(track, step);
}

uint8_t seq_edit_step_plock_get_at(seq_track_id_t track,
                                   seq_step_id_t step,
                                   uint8_t ordinal,
                                   seq_plock_entry_t *out_entry)
{
    return seq_model_step_plock_get_at(track, step, ordinal, out_entry);
}
