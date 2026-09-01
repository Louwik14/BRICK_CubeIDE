/*
 * Module: seq_edit
 * Role: Façade d'édition des pas (interaction hall/step, pages, gestes hold).
 * Responsibilities: mapping entrées vers steps, toggle/copy/paste/clear,
 * capture d'intentions d'édition et délégation au modèle/clipboard.
 * Integration: couche edition au-dessus de seq_model; hors scheduling audio temps réel.
 */
#include "Seq/seq_edit.h"

#include <string.h>

#include "App/engine_tasklet.h"
#include "Platform/memory_layout.h"
#include "Seq/seq_model.h"
#include "Track/entity_topology.h"
#include "Seq/seq_clipboard.h"
#include "Seq/seq_param_iface.h"
#include "App/Hall/hall_surface.h"
#include "Track/track_runtime.h"
#include "Storage/undo_v2.h"
#include "Seq/seq_runtime_control.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"

#define SEQ_EDIT_ENGINE_TICKS_PER_SECOND 1500U

#if defined(BRICK6_VARIANT_LOWCOST)
#define STEP_PLOCK_HOLD_MS 300U
#else
#define SEQ_STEP_HOLD_THRESHOLD_TICKS 160U
#define STEP_PLOCK_HOLD_MS (((SEQ_STEP_HOLD_THRESHOLD_TICKS * 1000U) + (SEQ_EDIT_ENGINE_TICKS_PER_SECOND - 1U)) \
                            / SEQ_EDIT_ENGINE_TICKS_PER_SECOND)
#endif

#if !defined(SEQ_STEP_HOLD_THRESHOLD_TICKS)
#define SEQ_STEP_HOLD_THRESHOLD_TICKS (((STEP_PLOCK_HOLD_MS * SEQ_EDIT_ENGINE_TICKS_PER_SECOND) + 999U) / 1000U)
#endif

typedef struct
{
    uint8_t pending[SEQ_STEPS_PER_PAGE];
    uint8_t held[SEQ_STEPS_PER_PAGE];
    uint8_t auto_note_pending[SEQ_STEPS_PER_PAGE];
    uint8_t edited[SEQ_STEPS_PER_PAGE];
    uint8_t pressed_active[SEQ_STEPS_PER_PAGE];
    seq_step_content_t pressed_content[SEQ_STEPS_PER_PAGE];
    uint8_t quick_length_applied;
    seq_edit_held_content_t held_content;
    uint8_t captured_note_count[128U];
    uint8_t note_capture_target_valid;
    seq_track_id_t note_capture_track;
    uint8_t note_capture_step_count;
    seq_step_id_t note_capture_steps[SEQ_STEPS_PER_PAGE];
    uint8_t note_capture_note_count;
    uint8_t note_capture_notes[SEQ_PLAY_MAX_CAPACITY];
    uint8_t note_capture_velocities[SEQ_PLAY_MAX_CAPACITY];
    uint8_t note_capture_undo_open;
    uint32_t press_tick[SEQ_STEPS_PER_PAGE];
    seq_step_id_t step_id[SEQ_STEPS_PER_PAGE];
    seq_track_id_t track_id[SEQ_STEPS_PER_PAGE];
} seq_edit_hold_state_t;

#if defined(BRICK6_VARIANT_LOWCOST)
#define SEQ_EDIT_LENGTH_FLASH_HALF_TICKS 150U
#define SEQ_EDIT_LENGTH_FLASH_PHASE_COUNT 4U

typedef struct
{
    uint8_t active;
    seq_track_id_t track;
    seq_step_id_t start_step;
    seq_step_id_t end_step;
    uint32_t start_tick;
} seq_edit_length_flash_t;
#endif

static void seq_edit_mark_step_edited(seq_track_id_t track, seq_step_id_t step);
static void seq_edit_clear_auto_note_pending(seq_track_id_t track, seq_step_id_t step);
static void seq_edit_finish_snapshot_undo(uint8_t started);

SEQ_STATE_D2 static seq_edit_hold_state_t g_seq_hold_state;
#if defined(BRICK6_VARIANT_LOWCOST)
SEQ_STATE_D2 static seq_edit_length_flash_t g_seq_length_flash;
#endif

static void seq_edit_reset_gesture_if_idle(void)
{
    for (uint8_t hall = 0U; hall < SEQ_STEPS_PER_PAGE; ++hall)
    {
        if ((g_seq_hold_state.pending[hall] != 0U)
                || (g_seq_hold_state.held[hall] != 0U))
        {
            return;
        }
    }

    g_seq_hold_state.quick_length_applied = 0U;
    g_seq_hold_state.held_content = SEQ_EDIT_HELD_CONTENT_NONE;
    seq_edit_finish_snapshot_undo(g_seq_hold_state.note_capture_undo_open);
    g_seq_hold_state.note_capture_undo_open = 0U;
    g_seq_hold_state.note_capture_target_valid = 0U;
}

#if defined(BRICK6_VARIANT_LOWCOST)
static uint8_t seq_edit_step_plock_upsert_succeeded(seq_plock_op_status_t status)
{
    return ((status == SEQ_PLOCK_OP_CREATED) || (status == SEQ_PLOCK_OP_UPDATED)) ? 1U : 0U;
}
#endif

uint8_t seq_edit_step_play_get(seq_track_id_t track,
                                seq_step_id_t step,
                                uint8_t voice,
                                seq_step_play_field_t field,
                                int16_t *out_value)
{
    return seq_model_play_get(track, step, voice, field, out_value);
}

seq_plock_op_status_t seq_edit_step_play_upsert(seq_track_id_t track,
                                                 seq_step_id_t step,
                                                 uint8_t voice,
                                                 seq_step_play_field_t field,
                                                 int16_t value)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return SEQ_PLOCK_OP_INVALID;
    }
    int16_t previous = 0;
    const uint8_t existed = seq_model_play_get(track, step, voice, field, &previous);
    if (seq_model_play_set(track, step, voice, field, value) == 0U)
    {
        return SEQ_PLOCK_OP_INVALID;
    }
    return (existed != 0U) ? SEQ_PLOCK_OP_UPDATED : SEQ_PLOCK_OP_CREATED;
}

void seq_edit_step_play_commit(seq_track_id_t track,
                               seq_step_id_t step,
                               uint8_t voice,
                               seq_step_play_field_t field)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return;
    }
    if (field == SEQ_STEP_PLAY_FIELD_NOTE)
    {
        seq_model_set_trig(track, step, 1U);
    }
    seq_edit_mark_step_edited(track, step);
    if (seq_model_step_is_active(track, step) == 0U)
    {
        seq_model_set_trig(track, step, 1U);
    }
    seq_edit_clear_auto_note_pending(track, step);
    seq_runtime_on_step_play_changed(track, step, voice, field);
}

seq_plock_op_status_t seq_edit_step_play_delete(seq_track_id_t track,
                                                 seq_step_id_t step,
                                                 uint8_t voice,
                                                 seq_step_play_field_t field)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return SEQ_PLOCK_OP_INVALID;
    }
    if (seq_model_play_clear(track, step, voice, field) == 0U)
        return SEQ_PLOCK_OP_NOT_FOUND;
    seq_runtime_on_step_play_changed(track, step, voice, field);
    return SEQ_PLOCK_OP_DELETED;
}

void seq_edit_step_play_clear_voice(seq_track_id_t track,
                                    seq_step_id_t step,
                                    uint8_t voice)
{
    if (seq_edit_track_sequence_is_locked(track) == 0U)
    {
        seq_model_play_clear_item(track, step, voice);
        seq_runtime_on_step_play_removed(track, step, (int16_t)voice);
    }
}

void seq_edit_step_play_clear(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_edit_track_sequence_is_locked(track) == 0U)
    {
        seq_model_play_clear_step(track, step);
        seq_runtime_on_step_play_removed(track, step, -1);
    }
}

uint8_t seq_edit_track_sequence_is_locked(seq_track_id_t track)
{
    if (entity_topology_is_active((brick_entity_id_t)track) == 0U)
    {
        return 1U;
    }

    return 0U;
}

uint8_t seq_edit_set_track_length(seq_track_id_t track, uint8_t length)
{
    if ((seq_edit_track_sequence_is_locked(track) != 0U)
            || (length < 1U) || (length > SEQ_MAX_STEPS)) return 0U;
    if (seq_model_get_track_length(track) == length) return 1U;
    seq_model_set_track_length(track, length);
    seq_runtime_on_track_length_changed(track);
    return 1U;
}

uint8_t seq_edit_set_track_division(seq_track_id_t track, uint8_t division)
{
    if ((seq_edit_track_sequence_is_locked(track) != 0U)
            || ((division != 1U) && (division != 2U)
                && (division != 4U) && (division != 8U))) return 0U;
    seq_runtime_set_track_div(track, division);
    return 1U;
}

uint8_t seq_edit_set_track_quantization(seq_track_id_t track, uint8_t quantization)
{
    if ((seq_edit_track_sequence_is_locked(track) != 0U) || (quantization > 100U)) return 0U;
    seq_runtime_set_track_quant(track, quantization);
    return 1U;
}

uint8_t seq_edit_set_track_swing(seq_track_id_t track, uint8_t swing)
{
    if ((seq_edit_track_sequence_is_locked(track) != 0U) || (swing > 100U)) return 0U;
    seq_runtime_set_track_swing(track, swing);
    return 1U;
}

static uint8_t seq_edit_begin_snapshot_undo(seq_track_id_t track,
                                            const seq_step_id_t *steps,
                                            uint8_t step_count)
{
    if ((steps == 0) || (step_count == 0U)
            || (undo_v2_begin_sequence_transaction(track,
                                                    steps,
                                                    step_count) != UNDO_V2_STATUS_OK))
    {
        return 0U;
    }

    return 1U;
}

static void seq_edit_finish_snapshot_undo(uint8_t started)
{
    if (started == 0U)
    {
        return;
    }

    if (undo_v2_commit_sequence_transaction() != UNDO_V2_STATUS_OK)
    {
        undo_v2_cancel_transaction();
        return;
    }
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

#if defined(BRICK6_VARIANT_LOWCOST)
static void seq_edit_lowcost_length_flash_start(seq_track_id_t track,
                                                seq_step_id_t start_step,
                                                seq_step_id_t end_step)
{
    g_seq_length_flash.active = 1U;
    g_seq_length_flash.track = track;
    g_seq_length_flash.start_step = start_step;
    g_seq_length_flash.end_step = end_step;
    g_seq_length_flash.start_tick = engine_tick_count;
}

uint8_t seq_edit_lowcost_length_flash_step_visible(seq_track_id_t track,
                                                   seq_step_id_t step)
{
    if ((g_seq_length_flash.active == 0U)
            || (track != g_seq_length_flash.track)
            || (step < g_seq_length_flash.start_step)
            || (step > g_seq_length_flash.end_step))
    {
        return 0U;
    }

    const uint32_t elapsed = engine_tick_count - g_seq_length_flash.start_tick;
    const uint32_t phase = elapsed / SEQ_EDIT_LENGTH_FLASH_HALF_TICKS;
    if (phase >= SEQ_EDIT_LENGTH_FLASH_PHASE_COUNT)
    {
        g_seq_length_flash.active = 0U;
        return 0U;
    }

    return ((phase & 0x1U) == 0U) ? 1U : 0U;
}

static uint8_t seq_edit_lowcost_step_has_play_param(seq_track_id_t track,
                                                    seq_step_id_t step,
                                                    uint8_t voice,
                                                    seq_step_play_field_t field)
{
    int16_t value = 0;
    return seq_edit_step_play_get(track, step, voice, field, &value);
}

static uint8_t seq_edit_lowcost_voice_is_present(seq_track_id_t track,
                                                 seq_step_id_t step,
                                                 uint8_t voice)
{
    return (uint8_t)((seq_edit_lowcost_step_has_play_param(track, step, voice, SEQ_STEP_PLAY_FIELD_NOTE) != 0U)
                    || (seq_edit_lowcost_step_has_play_param(track, step, voice, SEQ_STEP_PLAY_FIELD_VELOCITY) != 0U)
                    || (seq_edit_lowcost_step_has_play_param(track, step, voice, SEQ_STEP_PLAY_FIELD_LENGTH) != 0U));
}

static uint8_t seq_edit_lowcost_step_is_range_end_empty(seq_track_id_t track,
                                                        seq_step_id_t step)
{
    return (uint8_t)((seq_model_step_is_active(track, step) == 0U)
                    && (seq_model_step_has_non_play_plock(track, step) == 0U));
}

static uint8_t seq_edit_lowcost_source_is_held_or_pending(uint8_t hall)
{
    if (hall >= SEQ_STEPS_PER_PAGE)
    {
        return 0U;
    }

    if (g_seq_hold_state.held[hall] != 0U)
    {
        return 1U;
    }

    if (g_seq_hold_state.pending[hall] == 0U)
    {
        return 0U;
    }

    g_seq_hold_state.pending[hall] = 0U;
    g_seq_hold_state.held[hall] = 1U;
    return 1U;
}

static uint8_t seq_edit_lowcost_find_range_length_source(seq_track_id_t track,
                                                         seq_step_id_t end_step,
                                                         uint8_t end_hall,
                                                         seq_step_id_t *out_start_step)
{
    if (seq_model_is_step_editable_index(end_step) == 0U)
    {
        return 0U;
    }

    if (seq_edit_lowcost_step_is_range_end_empty(track, end_step) == 0U)
    {
        return 0U;
    }

    for (uint8_t hall = 0U; hall < SEQ_STEPS_PER_PAGE; ++hall)
    {
        if ((hall == end_hall)
                || (seq_edit_lowcost_source_is_held_or_pending(hall) == 0U)
                || (hall_surface_is_pressed(hall) == 0U)
                || (g_seq_hold_state.track_id[hall] != track)
                || (seq_model_step_produces_note(track, g_seq_hold_state.step_id[hall]) == 0U)
                || (g_seq_hold_state.step_id[hall] >= end_step))
        {
            continue;
        }
        if (out_start_step != 0)
        {
            *out_start_step = g_seq_hold_state.step_id[hall];
        }
        return 1U;
    }

    return 0U;
}

uint8_t seq_edit_lowcost_range_length_candidate(seq_track_id_t track,
                                                uint8_t hall_index)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return 0U;
    }

    seq_step_id_t end_step = 0U;
    if (seq_edit_map_hall_to_step(track, hall_index, &end_step) == 0U)
    {
        return 0U;
    }

    return seq_edit_lowcost_find_range_length_source(track, end_step, hall_index, 0);
}

static uint8_t seq_edit_lowcost_apply_length_to_voice(seq_track_id_t track,
                                                      seq_step_id_t step,
                                                      uint8_t voice,
                                                      uint8_t length_steps)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return 0U;
    }

    const seq_plock_op_status_t status =
        seq_edit_step_play_upsert(track, step, voice,
                                  SEQ_STEP_PLAY_FIELD_LENGTH, length_steps);

    return seq_edit_step_plock_upsert_succeeded(status);
}

static uint8_t seq_edit_lowcost_try_range_length(seq_track_id_t track,
                                                 uint8_t end_hall,
                                                 seq_step_id_t end_step)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return 0U;
    }

    seq_step_id_t start_step = 0U;
    if (seq_edit_lowcost_find_range_length_source(track, end_step, end_hall, &start_step) == 0U)
    {
        return 0U;
    }

    uint8_t div = 1U;
    (void)seq_runtime_get_track_div(track, &div);
    if ((div != 1U) && (div != 2U) && (div != 4U) && (div != 8U))
    {
        div = 1U;
    }

    uint32_t length_steps = ((uint32_t)end_step - (uint32_t)start_step + 1UL) * (uint32_t)div;
    if (length_steps < 1UL)
    {
        length_steps = 1UL;
    }
    if (length_steps > (uint32_t)SEQ_MAX_STEPS)
    {
        length_steps = (uint32_t)SEQ_MAX_STEPS;
    }

    uint8_t applied = 0U;
    const uint8_t play_capacity = seq_model_play_capacity(track);
    for (uint8_t voice = 0U; voice < play_capacity; ++voice)
    {
        if (seq_edit_lowcost_voice_is_present(track, start_step, voice) == 0U)
        {
            continue;
        }

        if (seq_edit_lowcost_apply_length_to_voice(track, start_step, voice, (uint8_t)length_steps) != 0U)
        {
            applied = 1U;
        }
    }

    if (applied == 0U)
    {
        applied = seq_edit_lowcost_apply_length_to_voice(track, start_step, 0U, (uint8_t)length_steps);
    }

    if (applied == 0U)
    {
        return 0U;
    }

    if (seq_model_step_is_active(track, start_step) == 0U)
    {
        seq_model_set_trig(track, start_step, 1U);
    }
    seq_edit_mark_step_edited(track, start_step);
    seq_edit_lowcost_length_flash_start(track, start_step, end_step);
    g_seq_hold_state.quick_length_applied = 1U;
    g_seq_hold_state.held_content = SEQ_EDIT_HELD_CONTENT_QUICK_LENGTH;
    return 1U;
}
#else
uint8_t seq_edit_lowcost_length_flash_step_visible(seq_track_id_t track,
                                                   seq_step_id_t step)
{
    (void)track;
    (void)step;
    return 0U;
}

uint8_t seq_edit_lowcost_range_length_candidate(seq_track_id_t track,
                                                uint8_t hall_index)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return 0U;
    }
    (void)hall_index;
    return 0U;
}
#endif

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
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return;
    }

    const uint8_t undo_started = seq_edit_begin_snapshot_undo(track, &step, 1U);
    if ((g_seq_hold_state.pressed_active[hall] == 0U)
            && (g_seq_hold_state.pressed_content[hall] == SEQ_STEP_CONTENT_EMPTY)
            && (g_seq_hold_state.auto_note_pending[hall] != 0U)
            && (seq_model_step_is_quick_note_eligible(track, step) != 0U))
    {
        seq_model_set_trig(track, step, 1U);
        seq_edit_finish_snapshot_undo(undo_started);
        return;
    }

    if (seq_model_step_is_active(track, step) != 0U)
    {
        seq_model_set_trig(track, step, 0U);
    }
    else
    {
        seq_model_set_trig(track, step, 1U);
    }
    seq_edit_finish_snapshot_undo(undo_started);
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
    seq_edit_reset_gesture_if_idle();
}

void seq_edit_init(void)
{
    seq_clipboard_init();
    memset(&g_seq_hold_state, 0, sizeof(g_seq_hold_state));
#if defined(BRICK6_VARIANT_LOWCOST)
    memset(&g_seq_length_flash, 0, sizeof(g_seq_length_flash));
#endif
}

uint8_t seq_edit_toggle_hall_step(seq_track_id_t track, uint8_t hall_index)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return 0U;
    }

    seq_step_id_t step = 0U;
    if (seq_edit_map_hall_to_step(track, hall_index, &step) == 0U)
    {
        return 0U;
    }

    const uint8_t undo_started = seq_edit_begin_snapshot_undo(track, &step, 1U);
    seq_model_toggle_trig(track, step);
    seq_edit_finish_snapshot_undo(undo_started);
    return 1U;
}

void seq_edit_change_page(seq_track_id_t track, int8_t delta)
{
    if (delta != 0)
    {
        seq_edit_note_capture_reset();
    }

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
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        seq_edit_reset_hall_press_state(hall_index);
        return;
    }

    seq_step_id_t step = 0U;
    if (seq_edit_map_hall_to_step(track, hall_index, &step) == 0U)
    {
        return;
    }

#if defined(BRICK6_VARIANT_LOWCOST)
    if (seq_edit_lowcost_try_range_length(track, hall_index, step) != 0U)
    {
        return;
    }
#endif

    g_seq_hold_state.step_id[hall_index] = step;
    g_seq_hold_state.track_id[hall_index] = track;
    g_seq_hold_state.pressed_active[hall_index] = seq_model_step_is_active(track, step);
    g_seq_hold_state.pressed_content[hall_index] = seq_model_get_step_content(track, step);
    g_seq_hold_state.auto_note_pending[hall_index] = seq_model_step_is_quick_note_eligible(track, step);
    g_seq_hold_state.edited[hall_index] = 0U;
    g_seq_hold_state.pending[hall_index] = 1U;
    g_seq_hold_state.held[hall_index] = 0U;
    g_seq_hold_state.press_tick[hall_index] = engine_tick_count;
    if (g_seq_hold_state.quick_length_applied == 0U)
    {
        g_seq_hold_state.held_content = SEQ_EDIT_HELD_CONTENT_NONE;
    }
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

        if (hall_surface_is_pressed(hall) == 0U)
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

seq_edit_held_content_t seq_edit_classify_held_steps(void)
{
    if (g_seq_hold_state.quick_length_applied != 0U)
    {
        return SEQ_EDIT_HELD_CONTENT_QUICK_LENGTH;
    }

    seq_track_id_t track = 0U;
    seq_step_id_t steps[SEQ_STEPS_PER_PAGE];
    const uint8_t count = seq_edit_collect_held_steps(&track,
                                                      steps,
                                                      (uint8_t)SEQ_STEPS_PER_PAGE,
                                                      1U);
    if (count == 0U)
    {
        g_seq_hold_state.held_content = SEQ_EDIT_HELD_CONTENT_NONE;
        return SEQ_EDIT_HELD_CONTENT_NONE;
    }

    const uint8_t first_empty = seq_model_step_is_empty(track, steps[0]);
    for (uint8_t i = 1U; i < count; ++i)
    {
        if (seq_model_step_is_empty(track, steps[i]) != first_empty)
        {
            g_seq_hold_state.held_content = SEQ_EDIT_HELD_CONTENT_MIXED;
            return SEQ_EDIT_HELD_CONTENT_MIXED;
        }
    }

    g_seq_hold_state.held_content = (first_empty != 0U)
        ? SEQ_EDIT_HELD_CONTENT_ALL_EMPTY
        : SEQ_EDIT_HELD_CONTENT_ALL_FILLED;
    return g_seq_hold_state.held_content;
}

uint8_t seq_edit_prepare_held_note_capture(seq_track_id_t *out_track,
                                            seq_step_id_t *out_steps,
                                            uint8_t max_steps,
                                            uint8_t *out_count)
{
    if (out_count != 0)
    {
        *out_count = 0U;
    }
    if ((out_track == 0) || (out_steps == 0) || (out_count == 0)
            || (max_steps == 0U))
    {
        return 0U;
    }

    const seq_edit_held_content_t content = seq_edit_classify_held_steps();
    if ((content != SEQ_EDIT_HELD_CONTENT_ALL_EMPTY)
            && (content != SEQ_EDIT_HELD_CONTENT_ALL_FILLED))
    {
        return 0U;
    }

    const uint8_t count = seq_edit_collect_held_steps(out_track,
                                                      out_steps,
                                                      max_steps,
                                                      1U);
    if (count == 0U)
    {
        return 0U;
    }

    *out_count = count;
    return 1U;
}

static uint8_t seq_edit_replace_step_play_notes_impl(seq_track_id_t track,
                                                     const seq_step_id_t *steps,
                                                     uint8_t step_count,
                                                     const uint8_t *notes,
                                                     const uint8_t *velocities,
                                                     uint8_t note_count,
                                                     uint8_t with_undo)
{
    if ((steps == 0) || (notes == 0) || (velocities == 0)
            || (step_count == 0U) || (step_count > (uint8_t)SEQ_MAX_STEPS)
            || (note_count == 0U) || (note_count > seq_model_play_capacity(track))
            || (seq_edit_track_sequence_is_locked(track) != 0U)
            || (seq_model_track_can_store_play(track) == 0U))
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < step_count; ++i)
    {
        if (seq_model_is_step_editable_index(steps[i]) == 0U)
        {
            return 0U;
        }
        for (uint8_t j = 0U; j < i; ++j)
        {
            if (steps[j] == steps[i])
            {
                return 0U;
            }
        }
    }

    for (uint8_t voice = 0U; voice < note_count; ++voice)
    {
        if ((notes[voice] > 127U) || (velocities[voice] > 127U))
        {
            return 0U;
        }
    }

    const uint8_t undo_started = (with_undo != 0U)
        ? seq_edit_begin_snapshot_undo(track, steps, step_count)
        : 0U;

    for (uint8_t i = 0U; i < step_count; ++i)
    {
        const seq_step_id_t step = steps[i];
        const uint8_t play_capacity = seq_model_play_capacity(track);
        for (uint8_t voice = 0U; voice < play_capacity; ++voice)
        {
            (void)seq_model_play_clear(track,
                                              step,
                                              voice,
                                              SEQ_STEP_PLAY_FIELD_NOTE);
            (void)seq_model_play_clear(track,
                                              step,
                                              voice,
                                              SEQ_STEP_PLAY_FIELD_VELOCITY);
            (void)seq_model_play_clear(track,
                                              step,
                                              voice,
                                              SEQ_STEP_PLAY_FIELD_MICROTIMING);
        }

        for (uint8_t voice = 0U; voice < note_count; ++voice)
        {
            if ((seq_model_play_set(track,
                                         step,
                                         voice,
                                         SEQ_STEP_PLAY_FIELD_NOTE,
                                         (int16_t)notes[voice]) == 0U)
                    || (seq_model_play_set(track,
                                                step,
                                                voice,
                                                SEQ_STEP_PLAY_FIELD_VELOCITY,
                                                (int16_t)velocities[voice]) == 0U)
                    || (seq_model_play_set(track,
                                                step,
                                                voice,
                                                SEQ_STEP_PLAY_FIELD_MICROTIMING,
                                                0) == 0U))
            {
                if (undo_started != 0U)
                {
                    undo_v2_cancel_transaction();
                }
                return 0U;
            }
        }

        seq_model_set_trig(track, step, 1U);
        seq_edit_mark_step_edited(track, step);
        seq_edit_clear_auto_note_pending(track, step);
    }

    if (with_undo != 0U)
    {
        seq_edit_finish_snapshot_undo(undo_started);
    }
    return 1U;
}

uint8_t seq_edit_replace_step_play_notes(seq_track_id_t track,
                                         const seq_step_id_t *steps,
                                         uint8_t step_count,
                                         const uint8_t *notes,
                                         const uint8_t *velocities,
                                         uint8_t note_count)
{
    return seq_edit_replace_step_play_notes_impl(track,
                                                 steps,
                                                 step_count,
                                                 notes,
                                                 velocities,
                                                 note_count,
                                                 1U);
}

uint8_t seq_edit_capture_held_note_on(uint8_t note, uint8_t velocity)
{
    if ((note >= 128U) || (velocity == 0U))
    {
        return 0U;
    }

    if (g_seq_hold_state.note_capture_target_valid == 0U)
    {
        seq_step_id_t steps[SEQ_STEPS_PER_PAGE];
        uint8_t step_count = 0U;
        if (seq_edit_prepare_held_note_capture(&g_seq_hold_state.note_capture_track,
                                                steps,
                                                (uint8_t)SEQ_STEPS_PER_PAGE,
                                                &step_count) == 0U)
        {
            return 0U;
        }

        memcpy(g_seq_hold_state.note_capture_steps,
               steps,
               (size_t)step_count * sizeof(steps[0]));
        g_seq_hold_state.note_capture_step_count = step_count;
        g_seq_hold_state.note_capture_note_count = 0U;
        if (seq_edit_begin_snapshot_undo(g_seq_hold_state.note_capture_track,
                                          g_seq_hold_state.note_capture_steps,
                                          g_seq_hold_state.note_capture_step_count) == 0U)
        {
            g_seq_hold_state.note_capture_step_count = 0U;
            return 0U;
        }
        g_seq_hold_state.note_capture_undo_open = 1U;
        g_seq_hold_state.note_capture_target_valid = 1U;
    }

    uint8_t voice = 0U;
    uint8_t existing = 0U;
    for (; voice < g_seq_hold_state.note_capture_note_count; ++voice)
    {
        if (g_seq_hold_state.note_capture_notes[voice] == note)
        {
            existing = 1U;
            break;
        }
    }

    if (existing == 0U)
    {
        const uint8_t play_capacity = seq_model_play_capacity(
            g_seq_hold_state.note_capture_track);
        if (g_seq_hold_state.note_capture_note_count >= play_capacity)
        {
            if (g_seq_hold_state.captured_note_count[note] < 0xFFU)
            {
                g_seq_hold_state.captured_note_count[note]++;
            }
            return 1U;
        }
        voice = g_seq_hold_state.note_capture_note_count++;
        g_seq_hold_state.note_capture_notes[voice] = note;
    }

    g_seq_hold_state.note_capture_velocities[voice] = velocity;
    if (seq_edit_replace_step_play_notes_impl(g_seq_hold_state.note_capture_track,
                                              g_seq_hold_state.note_capture_steps,
                                              g_seq_hold_state.note_capture_step_count,
                                              g_seq_hold_state.note_capture_notes,
                                              g_seq_hold_state.note_capture_velocities,
                                              g_seq_hold_state.note_capture_note_count,
                                              0U) == 0U)
    {
        if (existing == 0U)
        {
            g_seq_hold_state.note_capture_note_count--;
        }
        if (g_seq_hold_state.note_capture_undo_open != 0U)
        {
            undo_v2_cancel_transaction();
            g_seq_hold_state.note_capture_undo_open = 0U;
        }
        g_seq_hold_state.note_capture_target_valid = 0U;
        return 0U;
    }

    if (g_seq_hold_state.captured_note_count[note] < 0xFFU)
    {
        g_seq_hold_state.captured_note_count[note]++;
    }
    return 1U;
}

uint8_t seq_edit_note_capture_note_off(uint8_t note)
{
    if ((note >= 128U) || (g_seq_hold_state.captured_note_count[note] == 0U))
    {
        return 0U;
    }

    g_seq_hold_state.captured_note_count[note]--;
    uint8_t active = 0U;
    for (uint16_t i = 0U; i < 128U; ++i)
    {
        if (g_seq_hold_state.captured_note_count[i] != 0U)
        {
            active = 1U;
            break;
        }
    }
    if (active == 0U)
    {
        seq_edit_finish_snapshot_undo(g_seq_hold_state.note_capture_undo_open);
        g_seq_hold_state.note_capture_undo_open = 0U;
        g_seq_hold_state.note_capture_target_valid = 0U;
        g_seq_hold_state.note_capture_step_count = 0U;
        g_seq_hold_state.note_capture_note_count = 0U;
    }
    return 1U;
}

void seq_edit_note_capture_reset(void)
{
    seq_edit_finish_snapshot_undo(g_seq_hold_state.note_capture_undo_open);
    g_seq_hold_state.note_capture_undo_open = 0U;
    memset(g_seq_hold_state.captured_note_count,
           0,
           sizeof(g_seq_hold_state.captured_note_count));
    g_seq_hold_state.note_capture_target_valid = 0U;
    g_seq_hold_state.note_capture_track = 0U;
    g_seq_hold_state.note_capture_step_count = 0U;
    g_seq_hold_state.note_capture_note_count = 0U;
    memset(g_seq_hold_state.note_capture_steps,
           0,
           sizeof(g_seq_hold_state.note_capture_steps));
    memset(g_seq_hold_state.note_capture_notes,
           0,
           sizeof(g_seq_hold_state.note_capture_notes));
    memset(g_seq_hold_state.note_capture_velocities,
           0,
           sizeof(g_seq_hold_state.note_capture_velocities));
}

uint8_t seq_edit_step_is_pressed(seq_track_id_t track, seq_step_id_t step)
{
    for (uint8_t hall = 0U; hall < SEQ_STEPS_PER_PAGE; ++hall)
    {
        if (((g_seq_hold_state.pending[hall] != 0U) || (g_seq_hold_state.held[hall] != 0U))
                && (g_seq_hold_state.track_id[hall] == track)
                && (g_seq_hold_state.step_id[hall] == step)
                && (hall_surface_is_pressed(hall) != 0U))
        {
            return 1U;
        }
    }

    return 0U;
}

uint8_t seq_edit_adjust_held_step_roll(int8_t delta,
                                       seq_track_id_t *out_track,
                                       seq_step_id_t *out_step,
                                       uint8_t *out_roll)
{
    if (delta == 0)
    {
        return 0U;
    }

    seq_track_id_t held_track = 0U;
    seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
    const uint8_t held_count = seq_edit_collect_held_steps(&held_track,
                                                           held_steps,
                                                           (uint8_t)SEQ_STEPS_PER_PAGE,
                                                           1U);
    if ((held_count == 0U) || (seq_edit_track_sequence_is_locked(held_track) != 0U))
    {
        return 0U;
    }
    if (entity_topology_is_active((brick_entity_id_t)held_track) == 0U)
    {
        return 0U;
    }

    const uint8_t undo_started = seq_edit_begin_snapshot_undo(held_track,
                                                              held_steps,
                                                              held_count);
    uint8_t applied = 0U;
    for (uint8_t i = 0U; i < held_count; ++i)
    {
        const seq_step_id_t step = held_steps[i];
        if (seq_model_step_is_active(held_track, step) == 0U)
        {
            continue;
        }

        uint8_t roll = seq_model_get_step_roll(held_track, step);
        if (delta > 0)
        {
            if (roll < (uint8_t)(SEQ_STEP_ROLL_COUNT - 1U))
            {
                roll++;
            }
        }
        else if (roll > (uint8_t)SEQ_STEP_ROLL_OFF)
        {
            roll--;
        }

        seq_model_set_step_roll(held_track, step, roll);
        seq_runtime_on_step_roll_changed(held_track, step);
        seq_edit_mark_step_edited(held_track, step);
        seq_edit_clear_auto_note_pending(held_track, step);

        if (applied == 0U)
        {
            if (out_track != 0)
            {
                *out_track = held_track;
            }
            if (out_step != 0)
            {
                *out_step = step;
            }
            if (out_roll != 0)
            {
                *out_roll = roll;
            }
        }
        applied = 1U;
    }

    seq_edit_finish_snapshot_undo(undo_started);
    return applied;
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

uint8_t seq_edit_collect_pressed_steps(seq_track_id_t *out_track,
                                       seq_step_id_t *out_steps,
                                       uint8_t max_steps)
{
    if ((out_track == 0) || (out_steps == 0) || (max_steps == 0U))
    {
        return 0U;
    }

    uint8_t count = 0U;
    uint8_t track_set = 0U;

    for (uint8_t hall = 0U; hall < SEQ_STEPS_PER_PAGE; ++hall)
    {
        if ((g_seq_hold_state.pending[hall] == 0U) && (g_seq_hold_state.held[hall] == 0U))
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
                                 seq_param_slot_t param_slot,
                                 seq_plock_entry_t *out_entry)
{
    return seq_model_step_plock_find(track, step, set_id, param_slot, out_entry);
}

seq_plock_op_status_t seq_edit_step_plock_upsert(seq_track_id_t track,
                                                  seq_step_id_t step,
                                                  uint8_t set_id,
                                                  seq_param_slot_t param_slot,
                                                  seq_value16_t value16,
                                                  uint8_t flags)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return SEQ_PLOCK_OP_INVALID;
    }

    return seq_model_step_plock_upsert(track, step, set_id, param_slot, value16, flags);
}

void seq_edit_step_plock_commit(seq_track_id_t track,
                                seq_step_id_t step,
                                uint8_t set_id,
                                seq_param_slot_t param_slot)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return;
    }

    seq_edit_mark_step_edited(track, step);
    if (seq_model_step_is_active(track, step) == 0U)
    {
        seq_model_set_trig(track, step, 1U);
    }
    seq_edit_clear_auto_note_pending(track, step);
}

seq_plock_op_status_t seq_edit_step_plock_delete(seq_track_id_t track,
                                                  seq_step_id_t step,
                                                  uint8_t set_id,
                                                  seq_param_slot_t param_slot)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return SEQ_PLOCK_OP_INVALID;
    }

    const seq_plock_op_status_t status = seq_model_step_plock_delete(track, step, set_id, param_slot);
    return status;
}

void seq_edit_step_plock_clear(seq_track_id_t track, seq_step_id_t step)
{
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        return;
    }

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
    if (seq_edit_track_sequence_is_locked(track) != 0U)
    {
        if (out_result != 0)
        {
            memset(out_result, 0, sizeof(*out_result));
        }
        return 0U;
    }

    seq_step_id_t paste_targets[SEQ_MAX_STEPS];
    uint8_t paste_target_count = 0U;
    const uint8_t targets_resolved =
        seq_clipboard_collect_paste_targets(track,
                                            dest_steps,
                                            dest_count,
                                            paste_targets,
                                            (uint8_t)SEQ_MAX_STEPS,
                                            &paste_target_count);
    const uint8_t undo_started = ((targets_resolved != 0U) && (paste_target_count != 0U))
        ? seq_edit_begin_snapshot_undo(track, paste_targets, paste_target_count)
        : 0U;
    const uint8_t ok = seq_clipboard_paste(track, dest_steps, dest_count, out_result);
    seq_edit_finish_snapshot_undo(undo_started);
    if ((ok != 0U) && (dest_count != 0U))
        seq_play_scheduler_notify_track_pattern_change(track);
    return ok;
}

static uint8_t seq_edit_clear_steps_impl(seq_track_id_t track,
                                         const seq_step_id_t *steps,
                                         uint8_t step_count)
{
    uint8_t changed = 0U;
    for (uint8_t i = 0U; i < step_count; ++i)
    {
        const seq_step_id_t step = steps[i];
        if (seq_model_is_step_editable_index(step) == 0U)
        {
            continue;
        }

        if (seq_model_step_is_empty(track, step) == 0U)
            changed = 1U;
        seq_model_set_trig(track, step, 0U);
        seq_model_step_plock_clear(track, step);
        seq_model_play_clear_step(track, step);
    }
    return changed;
}

void seq_edit_clear_steps_without_undo(seq_track_id_t track,
                                       const seq_step_id_t *steps,
                                       uint8_t step_count)
{
    if ((steps == 0) || (seq_edit_track_sequence_is_locked(track) != 0U))
    {
        return;
    }

    if (seq_edit_clear_steps_impl(track, steps, step_count) != 0U)
        seq_play_scheduler_notify_track_pattern_change(track);
}

void seq_edit_clear_steps(seq_track_id_t track,
                          const seq_step_id_t *steps,
                          uint8_t step_count)
{
    if ((steps == 0) || (seq_edit_track_sequence_is_locked(track) != 0U))
    {
        return;
    }

    const uint8_t undo_started = seq_edit_begin_snapshot_undo(track,
                                                              steps,
                                                              step_count);
    if (seq_edit_clear_steps_impl(track, steps, step_count) != 0U)
        seq_play_scheduler_notify_track_pattern_change(track);
    seq_edit_finish_snapshot_undo(undo_started);
}
