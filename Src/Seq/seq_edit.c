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
#include "Core/track_state.h"
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

static uint8_t seq_edit_begin_snapshot_undo(uint8_t op,
                                            seq_track_id_t track,
                                            seq_step_id_t step,
                                            uint8_t extra)
{
    if (undo_v2_begin_snapshot_transaction(UNDO_V2_SOURCE_BUTTON,
                                           seq_edit_make_undo_gesture_key(op, track, step, extra)) != UNDO_V2_STATUS_OK)
    {
        return 0U;
    }

    if (undo_v2_capture_snapshot_before() != UNDO_V2_STATUS_OK)
    {
        undo_v2_cancel_transaction();
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

    if (undo_v2_capture_snapshot_after() != UNDO_V2_STATUS_OK)
    {
        undo_v2_cancel_transaction();
        return;
    }

    (void)undo_v2_commit_transaction();
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

static uint8_t seq_edit_lowcost_collect_length_targets(seq_track_id_t track,
                                                       uint8_t *out_members,
                                                       uint8_t out_capacity,
                                                       uint8_t *out_count,
                                                       uint8_t *out_group_master)
{
    if ((out_count == 0) || (out_group_master == 0))
    {
        return 0U;
    }

    *out_count = 0U;
    *out_group_master = 0U;

    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(track, &role_u8);
    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        return 0U;
    }

    if (role_u8 != (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        if ((out_members != 0) && (out_capacity > 0U))
        {
            out_members[0] = track;
        }
        *out_count = 1U;
        return 1U;
    }

    uint8_t collected_members[SEQ_TRACK_COUNT];
    uint8_t member_count = 0U;
    if ((track_runtime_collect_voice_group_members(track,
                                                   collected_members,
                                                   (uint8_t)(sizeof(collected_members) / sizeof(collected_members[0])),
                                                   &member_count) == 0U)
            || (member_count == 0U))
    {
        return 0U;
    }

    if (member_count > 8U)
    {
        member_count = 8U;
    }
    if ((out_members != 0) && (out_capacity < member_count))
    {
        return 0U;
    }
    for (uint8_t i = 0U; (out_members != 0) && (i < member_count); ++i)
    {
        out_members[i] = collected_members[i];
    }

    *out_count = member_count;
    *out_group_master = (member_count > 1U) ? 1U : 0U;
    return 1U;
}

static uint8_t seq_edit_lowcost_step_is_range_end_empty(seq_track_id_t track,
                                                        seq_step_id_t step)
{
    return (uint8_t)((seq_model_step_is_active(track, step) == 0U)
                    && (seq_model_step_has_non_play_plock(track, step) == 0U));
}

static uint8_t seq_edit_lowcost_source_is_held_or_mature_pending(uint8_t hall)
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

    if ((engine_tick_count - g_seq_hold_state.press_tick[hall]) < SEQ_STEP_HOLD_THRESHOLD_TICKS)
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

    uint8_t members[8U];
    uint8_t member_count = 0U;
    uint8_t unused_group_master = 0U;
    if (seq_edit_lowcost_collect_length_targets(track,
                                                members,
                                                (uint8_t)(sizeof(members) / sizeof(members[0])),
                                                &member_count,
                                                &unused_group_master) == 0U)
    {
        return 0U;
    }
    (void)unused_group_master;

    for (uint8_t i = 0U; i < member_count; ++i)
    {
        if (seq_edit_lowcost_step_is_range_end_empty(members[i], end_step) == 0U)
        {
            return 0U;
        }
    }

    for (uint8_t hall = 0U; hall < SEQ_STEPS_PER_PAGE; ++hall)
    {
        if ((hall == end_hall)
                || (seq_edit_lowcost_source_is_held_or_mature_pending(hall) == 0U)
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

    const uint8_t undo_started = seq_edit_begin_snapshot_undo(8U, track, start_step, (uint8_t)(end_step - start_step));
    uint8_t applied = 0U;
    uint8_t members[8U];
    uint8_t member_count = 0U;
    uint8_t group_master = 0U;
    if (seq_edit_lowcost_collect_length_targets(track,
                                                members,
                                                (uint8_t)(sizeof(members) / sizeof(members[0])),
                                                &member_count,
                                                &group_master) == 0U)
    {
        seq_edit_finish_snapshot_undo(0U);
        if (undo_started != 0U)
        {
            undo_v2_cancel_transaction();
        }
        return 0U;
    }

    if (group_master != 0U)
    {
        for (uint8_t i = 0U; i < member_count; ++i)
        {
            if (seq_edit_lowcost_apply_length_to_voice(members[i], start_step, 0U, (uint8_t)length_steps) != 0U)
            {
                applied = 1U;
            }
        }
    }
    else
    {
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
    }

    if (applied == 0U)
    {
        if (undo_started != 0U)
        {
            undo_v2_cancel_transaction();
        }
        return 0U;
    }

    if (seq_model_step_is_active(track, start_step) == 0U)
    {
        seq_model_set_trig(track, start_step, 1U);
    }
    seq_edit_finish_snapshot_undo(undo_started);
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
    (void)track;
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
    const uint8_t undo_started = seq_edit_begin_snapshot_undo(0U, track, step, hall);
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
    seq_step_id_t step = 0U;
    if (seq_edit_map_hall_to_step(track, hall_index, &step) == 0U)
    {
        return 0U;
    }

    const uint8_t undo_started = seq_edit_begin_snapshot_undo(1U, track, step, hall_index);
    seq_model_toggle_trig(track, step);
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
#if defined(BRICK6_VARIANT_LOWCOST)
            const uint32_t now_tick = engine_tick_count;
            if ((now_tick - g_seq_hold_state.press_tick[hall]) < SEQ_STEP_HOLD_THRESHOLD_TICKS)
            {
                continue;
            }
#endif
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
    return seq_model_step_plock_upsert(track, step, set_id, param_slot, value16, flags);
}

void seq_edit_step_plock_commit(seq_track_id_t track,
                                seq_step_id_t step,
                                uint8_t set_id,
                                seq_param_slot_t param_slot)
{
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
    seq_plock_entry_t before_entry;
    const uint8_t before_present = seq_edit_step_plock_find(track, step, set_id, param_slot, &before_entry);
    const uint8_t before_trig = seq_model_get_trig(track, step);
    if (undo_v2_begin_transaction(UNDO_V2_TX_KIND_PLOCK,
                                  UNDO_V2_SOURCE_BUTTON,
                                  seq_edit_make_undo_gesture_key(4U, track, step, (uint8_t)(set_id ^ param_slot)),
                                  UNDO_V2_TX_MODE_DELTA) != UNDO_V2_STATUS_OK)
    {
        return SEQ_PLOCK_OP_NOT_FOUND;
    }
    const seq_plock_op_status_t status = seq_model_step_plock_delete(track, step, set_id, param_slot);
    if ((status == SEQ_PLOCK_OP_DELETED) || (status == SEQ_PLOCK_OP_NOT_FOUND))
    {
        (void)undo_v2_record_plock_change(track,
                                          step,
                                          set_id,
                                          param_slot,
                                          before_present,
                                          (before_present != 0U) ? before_entry.value16 : 0U,
                                          (before_present != 0U) ? before_entry.flags : 0U,
                                          before_trig,
                                          0U,
                                          0U,
                                          0U,
                                          before_trig);
        (void)undo_v2_commit_transaction();
    }
    else
    {
        undo_v2_cancel_transaction();
    }
    return status;
}

uint8_t seq_edit_step_plock_apply_state(seq_track_id_t track,
                                        seq_step_id_t step,
                                        uint8_t set_id,
                                        seq_param_slot_t param_slot,
                                        uint8_t present,
                                        seq_value16_t value16,
                                        uint8_t flags,
                                        uint8_t trig_active)
{
    if (present != 0U)
    {
        const seq_plock_op_status_t status = seq_model_step_plock_upsert(track, step, set_id, param_slot, value16, flags);
        if (seq_edit_step_plock_upsert_succeeded(status) == 0U)
        {
            return 0U;
        }
    }
    else
    {
        const seq_plock_op_status_t status = seq_model_step_plock_delete(track, step, set_id, param_slot);
        if ((status != SEQ_PLOCK_OP_DELETED) && (status != SEQ_PLOCK_OP_NOT_FOUND))
        {
            return 0U;
        }
    }

    seq_model_set_trig(track, step, (trig_active != 0U) ? 1U : 0U);
    return 1U;
}

void seq_edit_step_plock_clear(seq_track_id_t track, seq_step_id_t step)
{
    const uint8_t undo_started = seq_edit_begin_snapshot_undo(5U, track, step, 0U);
    seq_model_step_plock_clear(track, step);
    seq_edit_finish_snapshot_undo(undo_started);
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
    const uint8_t undo_started = seq_edit_begin_snapshot_undo(6U, track, first_step, dest_count);
    const uint8_t ok = seq_clipboard_paste(track, dest_steps, dest_count, out_result);
    seq_edit_finish_snapshot_undo(undo_started);
    return ok;
}

void seq_edit_clear_steps(seq_track_id_t track,
                          const seq_step_id_t *steps,
                          uint8_t step_count)
{
    if (steps == 0)
    {
        return;
    }

    const uint8_t undo_started = seq_edit_begin_snapshot_undo(7U, track, steps[0], step_count);
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
    seq_edit_finish_snapshot_undo(undo_started);
}
