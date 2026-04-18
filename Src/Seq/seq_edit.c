/*
 * Module: seq_edit
 * Role: Façade d'édition des pas (interaction hall/step, pages, gestes hold).
 * Responsibilities: mapping entrées vers steps, toggle/copy/paste/clear,
 * capture d'intentions d'édition et délégation au modèle/clipboard.
 * Integration: couche edition au-dessus de seq_model; hors scheduling audio temps réel.
 */
#include "Seq/seq_edit.h"

#include <string.h>

#include "Core/engine_tasklet.h"
#include "Storage/memory_layout.h"
#include "Seq/seq_model.h"
#include "Seq/seq_clipboard.h"
#include "Seq/seq_param_iface.h"
#include "App/Hall/hall_engine.h"
#include "param_registry.h"
#include "Storage/undo_v1.h"

#define SEQ_STEP_HOLD_THRESHOLD_TICKS 240U

typedef struct
{
    uint8_t pending[SEQ_STEPS_PER_PAGE];
    uint8_t held[SEQ_STEPS_PER_PAGE];
    uint8_t auto_note_pending[SEQ_STEPS_PER_PAGE];
    uint8_t edited[SEQ_STEPS_PER_PAGE];
    uint8_t pressed_active[SEQ_STEPS_PER_PAGE];
    seq_step_content_t pressed_content[SEQ_STEPS_PER_PAGE];
    uint32_t press_tick[SEQ_STEPS_PER_PAGE];
    seq_step_id_t step_id[SEQ_STEPS_PER_PAGE];
    seq_track_id_t track_id[SEQ_STEPS_PER_PAGE];
} seq_edit_hold_state_t;

SEQ_STATE_D2 static seq_edit_hold_state_t g_seq_hold_state;

static uint8_t seq_edit_step_plock_upsert_succeeded(seq_plock_op_status_t status)
{
    return ((status == SEQ_PLOCK_OP_CREATED) || (status == SEQ_PLOCK_OP_UPDATED)) ? 1U : 0U;
}

static uint32_t seq_edit_make_undo_gesture_key(uint8_t op,
                                               seq_track_id_t track,
                                               seq_step_id_t step,
                                               uint8_t extra)
{
    return (0x20000000UL
        | ((uint32_t)op << 24)
        | ((uint32_t)track << 16)
        | ((uint32_t)step << 8)
        | (uint32_t)extra);
}

static uint8_t seq_edit_is_play_note_param(uint8_t set_id, seq_param8_t param8)
{
    if (set_id != (uint8_t)SEQ_PLOCK_SET_PLAY)
    {
        return 0U;
    }

    return ((param8 == (seq_param8_t)PARAM_SEQ_PLAY_V1_NOTE)
            || (param8 == (seq_param8_t)PARAM_SEQ_PLAY_V2_NOTE)
            || (param8 == (seq_param8_t)PARAM_SEQ_PLAY_V3_NOTE)
            || (param8 == (seq_param8_t)PARAM_SEQ_PLAY_V4_NOTE)) ? 1U : 0U;
}

static void seq_edit_clear_auto_note_pending(seq_track_id_t track, seq_step_id_t step)
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

static void seq_edit_mark_step_edited(seq_track_id_t track, seq_step_id_t step)
{
    for (uint8_t hall = 0U; hall < SEQ_STEPS_PER_PAGE; ++hall)
    {
        if ((g_seq_hold_state.track_id[hall] == track)
                && (g_seq_hold_state.step_id[hall] == step))
        {
            g_seq_hold_state.edited[hall] = 1U;
        }
    }
}

static void seq_edit_apply_short_action(uint8_t hall)
{
    if (hall >= SEQ_STEPS_PER_PAGE)
    {
        return;
    }

    if (g_seq_hold_state.edited[hall] != 0U)
    {
        return;
    }

    const seq_track_id_t track = g_seq_hold_state.track_id[hall];
    const seq_step_id_t step = g_seq_hold_state.step_id[hall];
    undo_v1_begin_gesture(seq_edit_make_undo_gesture_key(0U, track, step, hall));
    if ((g_seq_hold_state.pressed_active[hall] == 0U)
            && (g_seq_hold_state.pressed_content[hall] == SEQ_STEP_CONTENT_EMPTY)
            && (g_seq_hold_state.auto_note_pending[hall] != 0U)
            && (seq_model_step_is_quick_note_eligible(track, step) != 0U))
    {
        (void)undo_v1_capture_before_edit(0U);
        seq_model_set_trig(track, step, 1U);
        float note_value = 60.0f;
        if (param_registry_get_track_value(PARAM_SEQ_PLAY_V1_NOTE, track, &note_value) == 0U)
        {
            note_value = param_get(PARAM_SEQ_PLAY_V1_NOTE);
        }

        const seq_value16_t encoded = seq_param_iface_encode_param_value(PARAM_SEQ_PLAY_V1_NOTE, note_value);
        const seq_plock_op_status_t status = seq_model_step_plock_upsert(track,
                                                                          step,
                                                                          (uint8_t)SEQ_PLOCK_SET_PLAY,
                                                                          (seq_param8_t)PARAM_SEQ_PLAY_V1_NOTE,
                                                                          encoded,
                                                                          0U);
        if (seq_edit_step_plock_upsert_succeeded(status) == 0U)
        {
            seq_model_set_trig(track, step, 0U);
        }
        return;
    }

    (void)undo_v1_capture_before_edit(0U);
    if (seq_model_step_is_active(track, step) != 0U)
    {
        seq_model_set_trig(track, step, 0U);
    }
    else
    {
        seq_model_set_trig(track, step, 1U);
    }
}

static void seq_edit_reset_hall_press_state(uint8_t hall)
{
    if (hall >= SEQ_STEPS_PER_PAGE)
    {
        return;
    }

    g_seq_hold_state.auto_note_pending[hall] = 0U;
    g_seq_hold_state.edited[hall] = 0U;
    g_seq_hold_state.pending[hall] = 0U;
    g_seq_hold_state.held[hall] = 0U;
}

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

    undo_v1_begin_gesture(seq_edit_make_undo_gesture_key(1U, track, step, hall_index));
    (void)undo_v1_capture_before_edit(0U);
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

    if (seq_model_is_step_editable_index(step) == 0U)
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
    g_seq_hold_state.pressed_active[hall_index] = seq_model_step_is_active(track, step);
    g_seq_hold_state.pressed_content[hall_index] = seq_model_get_step_content(track, step);
    g_seq_hold_state.auto_note_pending[hall_index] = seq_model_step_is_quick_note_eligible(track, step);
    g_seq_hold_state.edited[hall_index] = 0U;
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

    if ((was_pending != 0U) && (was_held == 0U))
    {
        const uint32_t held_ticks = engine_tick_count - g_seq_hold_state.press_tick[hall_index];
        if (held_ticks < SEQ_STEP_HOLD_THRESHOLD_TICKS)
        {
            seq_edit_apply_short_action(hall_index);
        }
    }

    seq_edit_reset_hall_press_state(hall_index);
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
            const uint32_t held_ticks = now_tick - g_seq_hold_state.press_tick[hall];
            if (held_ticks < SEQ_STEP_HOLD_THRESHOLD_TICKS)
            {
                seq_edit_apply_short_action(hall);
            }
            seq_edit_reset_hall_press_state(hall);
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
    (void)undo_v1_capture_before_edit(0U);
    return seq_model_step_plock_upsert(track, step, set_id, param8, value16, flags);
}

void seq_edit_step_plock_commit(seq_track_id_t track,
                                seq_step_id_t step,
                                uint8_t set_id,
                                seq_param8_t param8)
{
    if (seq_edit_is_play_note_param(set_id, param8) != 0U)
    {
        (void)undo_v1_capture_before_edit(0U);
        seq_model_set_trig(track, step, 1U);
    }

    seq_edit_mark_step_edited(track, step);
    if (seq_model_step_is_active(track, step) == 0U)
    {
        (void)undo_v1_capture_before_edit(0U);
        seq_model_set_trig(track, step, 1U);
    }
    seq_edit_clear_auto_note_pending(track, step);
}

seq_plock_op_status_t seq_edit_step_plock_delete(seq_track_id_t track,
                                                  seq_step_id_t step,
                                                  uint8_t set_id,
                                                  seq_param8_t param8)
{
    undo_v1_begin_gesture(seq_edit_make_undo_gesture_key(4U, track, step, (uint8_t)(set_id ^ param8)));
    (void)undo_v1_capture_before_edit(0U);
    return seq_model_step_plock_delete(track, step, set_id, param8);
}

void seq_edit_step_plock_clear(seq_track_id_t track, seq_step_id_t step)
{
    undo_v1_begin_gesture(seq_edit_make_undo_gesture_key(5U, track, step, 0U));
    (void)undo_v1_capture_before_edit(0U);
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
    seq_step_id_t first_step = 0U;
    if ((dest_steps != 0) && (dest_count != 0U))
    {
        first_step = dest_steps[0];
    }
    undo_v1_begin_gesture(seq_edit_make_undo_gesture_key(6U, track, first_step, dest_count));
    (void)undo_v1_capture_before_edit(0U);
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

    undo_v1_begin_gesture(seq_edit_make_undo_gesture_key(7U, track, steps[0], step_count));
    (void)undo_v1_capture_before_edit(0U);
    for (uint8_t i = 0U; i < step_count; ++i)
    {
        const seq_step_id_t step = steps[i];
        if (seq_model_is_step_editable_index(step) == 0U)
        {
            continue;
        }

        seq_model_set_trig(track, step, 0U);
        seq_model_step_plock_clear(track, step);
    }
}
