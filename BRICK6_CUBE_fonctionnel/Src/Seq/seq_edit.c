#include "Seq/seq_edit.h"

#include <string.h>

#include "Core/engine_tasklet.h"
#include "Storage/memory_layout.h"
#include "Seq/seq_model.h"
#include "Seq/seq_clipboard.h"
#include "Seq/seq_param_iface.h"
#include "App/Hall/hall_engine.h"
#include "param_registry.h"
#include "param_store.h"

#define SEQ_STEP_HOLD_THRESHOLD_TICKS 120U

typedef struct
{
    uint8_t pending[SEQ_STEPS_PER_PAGE];
    uint8_t held[SEQ_STEPS_PER_PAGE];
    uint8_t auto_note_pending[SEQ_STEPS_PER_PAGE];
    uint32_t press_tick[SEQ_STEPS_PER_PAGE];
    seq_step_id_t step_id[SEQ_STEPS_PER_PAGE];
    seq_track_id_t track_id[SEQ_STEPS_PER_PAGE];
} seq_edit_hold_state_t;

SEQ_STATE_D2 static seq_edit_hold_state_t g_seq_hold_state;

void seq_edit_init(void)
{
    seq_clipboard_init();
    memset(&g_seq_hold_state, 0, sizeof(g_seq_hold_state));
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

void seq_edit_step_press(seq_track_id_t track, uint8_t hall_index)
{
    if (hall_index >= SEQ_STEPS_PER_PAGE)
    {
        return;
    }

    seq_step_id_t step = 0U;
    if (seq_edit_map_hall_to_step(track, hall_index, &step) == 0U)
    {
        return;
    }

    g_seq_hold_state.step_id[hall_index] = step;
    g_seq_hold_state.track_id[hall_index] = track;

    if (seq_model_get_trig(track, step) == 0U)
    {
        seq_model_set_trig(track, step, 1U);
        g_seq_hold_state.auto_note_pending[hall_index] = 1U;
        g_seq_hold_state.pending[hall_index] = 0U;
        g_seq_hold_state.held[hall_index] = 1U;
        g_seq_hold_state.press_tick[hall_index] = engine_tick_count;
        return;
    }

    g_seq_hold_state.auto_note_pending[hall_index] = 0U;
    g_seq_hold_state.pending[hall_index] = 1U;
    g_seq_hold_state.held[hall_index] = 0U;
    g_seq_hold_state.press_tick[hall_index] = engine_tick_count;
}

void seq_edit_step_release(seq_track_id_t track, uint8_t hall_index)
{
    (void)track;

    if (hall_index >= SEQ_STEPS_PER_PAGE)
    {
        return;
    }

    const uint8_t was_pending = g_seq_hold_state.pending[hall_index];
    const uint8_t was_held = g_seq_hold_state.held[hall_index];
    const uint8_t was_auto_note_pending = g_seq_hold_state.auto_note_pending[hall_index];
    const seq_track_id_t held_track = g_seq_hold_state.track_id[hall_index];
    const seq_step_id_t held_step = g_seq_hold_state.step_id[hall_index];

    if ((was_pending != 0U) && (was_held == 0U))
    {
        seq_model_toggle_trig(held_track, held_step);
    }
    else if (was_auto_note_pending != 0U)
    {
        float note_value = 60.0f;
        if (param_registry_get_track_value(PARAM_SEQ_PLAY_V1_NOTE, held_track, &note_value) == 0U)
        {
            note_value = param_get(PARAM_SEQ_PLAY_V1_NOTE);
        }

        const seq_value16_t encoded = seq_param_iface_encode_param_value(PARAM_SEQ_PLAY_V1_NOTE, note_value);
        (void)seq_model_step_plock_upsert(held_track,
                                          held_step,
                                          (uint8_t)SEQ_PLOCK_SET_PLAY,
                                          (seq_param8_t)PARAM_SEQ_PLAY_V1_NOTE,
                                          encoded,
                                          0U);
    }

    g_seq_hold_state.auto_note_pending[hall_index] = 0U;
    g_seq_hold_state.pending[hall_index] = 0U;
    g_seq_hold_state.held[hall_index] = 0U;
}

void seq_edit_step_hold_update(void)
{
    const uint32_t now_tick = engine_tick_count;

    for (uint8_t hall = 0U; hall < SEQ_STEPS_PER_PAGE; ++hall)
    {
        if (g_seq_hold_state.pending[hall] == 0U)
        {
            continue;
        }

        if (hall_engine_is_pressed(hall) == 0U)
        {
            seq_model_toggle_trig(g_seq_hold_state.track_id[hall],
                                  g_seq_hold_state.step_id[hall]);
            g_seq_hold_state.auto_note_pending[hall] = 0U;
            g_seq_hold_state.pending[hall] = 0U;
            g_seq_hold_state.held[hall] = 0U;
            continue;
        }

        if ((now_tick - g_seq_hold_state.press_tick[hall]) >= SEQ_STEP_HOLD_THRESHOLD_TICKS)
        {
            g_seq_hold_state.held[hall] = 1U;
            g_seq_hold_state.pending[hall] = 0U;
        }
    }
}

uint8_t seq_edit_collect_held_steps(seq_track_id_t *out_track,
                                    seq_step_id_t *out_steps,
                                    uint8_t max_steps,
                                    uint8_t promote_pending)
{
    if ((out_track == 0) || (out_steps == 0) || (max_steps == 0U))
    {
        return 0U;
    }

    uint8_t count = 0U;
    uint8_t track_set = 0U;

    for (uint8_t hall = 0U; hall < SEQ_STEPS_PER_PAGE; ++hall)
    {
        uint8_t selected = g_seq_hold_state.held[hall];
        if ((selected == 0U) && (promote_pending != 0U) && (g_seq_hold_state.pending[hall] != 0U))
        {
            g_seq_hold_state.pending[hall] = 0U;
            g_seq_hold_state.held[hall] = 1U;
            selected = 1U;
        }

        if (selected == 0U)
        {
            continue;
        }

        if (track_set == 0U)
        {
            *out_track = g_seq_hold_state.track_id[hall];
            track_set = 1U;
        }

        if (g_seq_hold_state.track_id[hall] != *out_track)
        {
            continue;
        }

        if (count < max_steps)
        {
            out_steps[count] = g_seq_hold_state.step_id[hall];
            count++;
        }
    }

    return count;
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
    if (set_id != (uint8_t)SEQ_PLOCK_SET_PLAY)
    {
        for (uint8_t hall = 0U; hall < SEQ_STEPS_PER_PAGE; ++hall)
        {
            if ((g_seq_hold_state.auto_note_pending[hall] != 0U)
                    && (g_seq_hold_state.track_id[hall] == track)
                    && (g_seq_hold_state.step_id[hall] == step))
            {
                g_seq_hold_state.auto_note_pending[hall] = 0U;
            }
        }
    }
    else if ((param8 == (seq_param8_t)PARAM_SEQ_PLAY_V1_NOTE)
             || (param8 == (seq_param8_t)PARAM_SEQ_PLAY_V2_NOTE)
             || (param8 == (seq_param8_t)PARAM_SEQ_PLAY_V3_NOTE)
             || (param8 == (seq_param8_t)PARAM_SEQ_PLAY_V4_NOTE))
    {
        for (uint8_t hall = 0U; hall < SEQ_STEPS_PER_PAGE; ++hall)
        {
            if ((g_seq_hold_state.auto_note_pending[hall] != 0U)
                    && (g_seq_hold_state.track_id[hall] == track)
                    && (g_seq_hold_state.step_id[hall] == step))
            {
                g_seq_hold_state.auto_note_pending[hall] = 0U;
            }
        }
    }

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

uint8_t seq_edit_copy_steps(seq_track_id_t track,
                            const seq_step_id_t *steps,
                            uint8_t step_count)
{
    return seq_clipboard_copy(track, steps, step_count);
}

uint8_t seq_edit_paste_steps(seq_track_id_t track,
                             const seq_step_id_t *dest_steps,
                             uint8_t dest_count,
                             seq_clipboard_paste_result_t *out_result)
{
    return seq_clipboard_paste(track, dest_steps, dest_count, out_result);
}

void seq_edit_clear_steps(seq_track_id_t track,
                          const seq_step_id_t *steps,
                          uint8_t step_count)
{
    if (steps == 0)
    {
        return;
    }

    for (uint8_t i = 0U; i < step_count; ++i)
    {
        const seq_step_id_t step = steps[i];
        if (step >= SEQ_MAX_STEPS)
        {
            continue;
        }

        seq_model_set_trig(track, step, 0U);
        seq_model_step_plock_clear(track, step);
    }
}
