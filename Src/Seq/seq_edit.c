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
#include "App/Hall/hall_surface.h"
#include "Core/track_runtime.h"
#include "param_registry.h"
#include "Storage/undo_v2.h"
#include "Seq/seq_runtime_control.h"

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

SEQ_STATE_D2 static seq_edit_hold_state_t g_seq_hold_state;
#if defined(BRICK6_VARIANT_LOWCOST)
SEQ_STATE_D2 static seq_edit_length_flash_t g_seq_length_flash;
#endif

static uint8_t seq_edit_step_plock_upsert_succeeded(seq_plock_op_status_t status)
{
    return ((status == SEQ_PLOCK_OP_CREATED) || (status == SEQ_PLOCK_OP_UPDATED)) ? 1U : 0U;
}

uint8_t seq_edit_track_sequence_is_locked(seq_track_id_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 1U;
    }

    return 0U;
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

static uint8_t seq_edit_is_play_note_param(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot)
{
    if (set_id != (uint8_t)SEQ_PLOCK_SET_PLAY)
    {
        return 0U;
    }

    param_id_t param = PARAM_COUNT;
    if (seq_param_iface_slot_to_param(track, set_id, param_slot, &param) == 0U)
    {
        return 0U;
    }

    return ((param == PARAM_SEQ_PLAY_V1_NOTE)
            || (param == PARAM_SEQ_PLAY_V2_NOTE)
            || (param == PARAM_SEQ_PLAY_V3_NOTE)
            || (param == PARAM_SEQ_PLAY_V4_NOTE)) ? 1U : 0U;
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

static param_id_t seq_edit_lowcost_length_param_for_voice(uint8_t voice)
{
    static const param_id_t k_len_params[4U] = {
        PARAM_SEQ_PLAY_V1_LEN,
        PARAM_SEQ_PLAY_V2_LEN,
        PARAM_SEQ_PLAY_V3_LEN,
        PARAM_SEQ_PLAY_V4_LEN
    };

    return (voice < 4U) ? k_len_params[voice] : PARAM_SEQ_PLAY_V1_LEN;
}

static param_id_t seq_edit_lowcost_note_param_for_voice(uint8_t voice)
{
    static const param_id_t k_note_params[4U] = {
        PARAM_SEQ_PLAY_V1_NOTE,
        PARAM_SEQ_PLAY_V2_NOTE,
        PARAM_SEQ_PLAY_V3_NOTE,
        PARAM_SEQ_PLAY_V4_NOTE
    };

    return (voice < 4U) ? k_note_params[voice] : PARAM_SEQ_PLAY_V1_NOTE;
}

static param_id_t seq_edit_lowcost_vel_param_for_voice(uint8_t voice)
{
    static const param_id_t k_vel_params[4U] = {
        PARAM_SEQ_PLAY_V1_VEL,
        PARAM_SEQ_PLAY_V2_VEL,
        PARAM_SEQ_PLAY_V3_VEL,
        PARAM_SEQ_PLAY_V4_VEL
    };

    return (voice < 4U) ? k_vel_params[voice] : PARAM_SEQ_PLAY_V1_VEL;
}

static uint8_t seq_edit_lowcost_step_has_play_param(seq_track_id_t track,
                                                    seq_step_id_t step,
                                                    param_id_t param)
{
    seq_param_slot_t slot = 0U;
    seq_plock_entry_t entry;
    return ((seq_param_iface_param_to_slot(track, (uint8_t)SEQ_PLOCK_SET_PLAY, param, &slot) != 0U)
            && (seq_model_step_plock_find(track, step, (uint8_t)SEQ_PLOCK_SET_PLAY, slot, &entry) != 0U))
        ? 1U
        : 0U;
}

static uint8_t seq_edit_lowcost_voice_is_present(seq_track_id_t track,
                                                 seq_step_id_t step,
                                                 uint8_t voice)
{
    return (uint8_t)((seq_edit_lowcost_step_has_play_param(track, step, seq_edit_lowcost_note_param_for_voice(voice)) != 0U)
                    || (seq_edit_lowcost_step_has_play_param(track, step, seq_edit_lowcost_vel_param_for_voice(voice)) != 0U)
                    || (seq_edit_lowcost_step_has_play_param(track, step, seq_edit_lowcost_length_param_for_voice(voice)) != 0U));
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

    const param_id_t len_param = seq_edit_lowcost_length_param_for_voice(voice);
    seq_param_slot_t len_slot = 0U;
    if (seq_param_iface_param_to_slot(track, (uint8_t)SEQ_PLOCK_SET_PLAY, len_param, &len_slot) == 0U)
    {
        return 0U;
    }

    const seq_value16_t encoded = seq_param_iface_encode_param_value(len_param, (float)length_steps);
    const seq_plock_op_status_t status =
        seq_model_step_plock_upsert(track, step, (uint8_t)SEQ_PLOCK_SET_PLAY, len_slot, encoded, 0U);

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
    for (uint8_t voice = 0U; voice < 4U; ++voice)
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
    if (track_topology_is_play(track) == 0U)
    {
        seq_model_toggle_special_action(track, step);
        seq_edit_finish_snapshot_undo(undo_started);
        return;
    }
    if ((g_seq_hold_state.pressed_active[hall] == 0U)
            && (g_seq_hold_state.pressed_content[hall] == SEQ_STEP_CONTENT_EMPTY)
            && (g_seq_hold_state.auto_note_pending[hall] != 0U)
            && (seq_model_step_is_quick_note_eligible(track, step) != 0U))
    {
        seq_model_set_trig(track, step, 1U);
        float note_value = 60.0f;
        if (param_registry_get_track_value(PARAM_SEQ_PLAY_V1_NOTE, track, &note_value) == 0U)
        {
            note_value = param_get(PARAM_SEQ_PLAY_V1_NOTE);
        }

        const seq_value16_t encoded = seq_param_iface_encode_param_value(PARAM_SEQ_PLAY_V1_NOTE, note_value);
        seq_param_slot_t note_slot = 0U;
        if (seq_param_iface_param_to_slot(track,
                                          (uint8_t)SEQ_PLOCK_SET_PLAY,
                                          PARAM_SEQ_PLAY_V1_NOTE,
                                          &note_slot) == 0U)
        {
            seq_model_set_trig(track, step, 0U);
            seq_edit_finish_snapshot_undo(undo_started);
            return;
        }
        const seq_plock_op_status_t status = seq_model_step_plock_upsert(track,
                                                                          step,
                                                                          (uint8_t)SEQ_PLOCK_SET_PLAY,
                                                                          note_slot,
                                                                          encoded,
                                                                          0U);
        if (seq_edit_step_plock_upsert_succeeded(status) == 0U)
        {
            seq_model_set_trig(track, step, 0U);
        }
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
    if (track_topology_is_play(track) != 0U)
    {
        seq_model_toggle_trig(track, step);
    }
    else
    {
        seq_model_toggle_special_action(track, step);
    }
    seq_edit_finish_snapshot_undo(undo_started);
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
    if (seq_edit_track_sequence_is_locked(held_track) != 0U)
    {
        return 0U;
    }
    if (track_topology_is_play(held_track) == 0U)
    {
        return 0U;
    }

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
        seq_edit_mark_step_edited(held_track, step);
        seq_edit_clear_auto_note_pending(held_track, step);

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
        return 1U;
    }

    return 0U;
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

    if (seq_edit_is_play_note_param(track, set_id, param_slot) != 0U)
    {
        seq_model_set_trig(track, step, 1U);
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

    const uint8_t undo_started = seq_edit_begin_snapshot_undo(track,
                                                              dest_steps,
                                                              dest_count);
    const uint8_t ok = seq_clipboard_paste(track, dest_steps, dest_count, out_result);
    seq_edit_finish_snapshot_undo(undo_started);
    return ok;
}

static void seq_edit_clear_steps_impl(seq_track_id_t track,
                                      const seq_step_id_t *steps,
                                      uint8_t step_count)
{
    for (uint8_t i = 0U; i < step_count; ++i)
    {
        const seq_step_id_t step = steps[i];
        if (seq_model_is_step_editable_index(step) == 0U)
        {
            continue;
        }

        if (track_topology_is_play(track) != 0U)
            seq_model_set_trig(track, step, 0U);
        else
            seq_model_set_special_action(track, step, (uint8_t)SEQ_SPECIAL_ACTION_NONE);
        seq_model_step_plock_clear(track, step);
    }
}

void seq_edit_clear_steps_without_undo(seq_track_id_t track,
                                       const seq_step_id_t *steps,
                                       uint8_t step_count)
{
    if ((steps == 0) || (seq_edit_track_sequence_is_locked(track) != 0U))
    {
        return;
    }

    seq_edit_clear_steps_impl(track, steps, step_count);
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
    seq_edit_clear_steps_impl(track, steps, step_count);
    seq_edit_finish_snapshot_undo(undo_started);
}
