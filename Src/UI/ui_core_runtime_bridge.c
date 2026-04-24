#include "ui_core_runtime_bridge.h"

#include "App/Hall/hall_engine.h"
#include "Core/brick6_master_buffer.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Keyboard/keyboard_runtime.h"
#include "Mod/mod_lfo_v1.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Storage/undo_v2.h"
#include "Storage/pattern_live_ram.h"
#include "audio_float.h"
#include "buttons.h"
#include "param_registry.h"
#include "ui_active_track_sync.h"
#include "ui_edit_context_sync.h"
#include "ui_core_navigation_bridge.h"
#include "ui_core_pattern.h"
#include "ui_core_seq_transport.h"
#include "ui_core_shortcuts.h"
#include "ui_macro_interaction.h"
#include "ui_system_sync_internal.h"

/*
 * Runtime bridge for ui_core:
 * - ui_core keeps arbitration and UI state.
 * - this module owns the explicit UI -> runtime execution seam.
 */
typedef struct
{
    const ui_system_sync_request_t *request;
    uint8_t sync_active_track_ui_context;
    uint8_t track;
    ui_track_family_t family;
    ui_track_type_t type;
    const uint8_t *family_data;
    const uint8_t *type_data;
    const uint8_t *midi_channel_data;
    const uint8_t *midi_source_data;
    ui_core_runtime_bridge_post_sync_fn post_sync;
} ui_core_runtime_bridge_track_transition_ctx_t;

static uint8_t ui_core_runtime_bridge_find_unique_master_buffer_track(uint8_t *out_track);
static void ui_core_runtime_bridge_prepare_track_transition_request(ui_system_sync_request_t *request,
                                                                    uint8_t active_track_touched);
static void ui_core_runtime_bridge_init_track_transition_ctx(ui_core_runtime_bridge_track_transition_ctx_t *ctx,
                                                             const ui_system_sync_request_t *request,
                                                             uint8_t sync_active_track_ui_context,
                                                             uint8_t track,
                                                             ui_core_runtime_bridge_post_sync_fn post_sync);
static void ui_core_runtime_bridge_init_bulk_track_transition_ctx(ui_core_runtime_bridge_track_transition_ctx_t *ctx,
                                                                  const ui_system_sync_request_t *request,
                                                                  const uint8_t family[UI_TRACK_COUNT],
                                                                  const uint8_t type[UI_TRACK_COUNT],
                                                                  const uint8_t midi_channel[UI_TRACK_COUNT],
                                                                  const uint8_t midi_source[UI_TRACK_COUNT],
                                                                  ui_core_runtime_bridge_post_sync_fn post_sync);

static uint8_t ui_core_runtime_bridge_track_is_master_buffer(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 0U;
    }

    return (uint8_t)((track_state_get_family(track) == UI_TRACK_FAMILY_MASTER)
            && (track_state_get_type(track) == UI_TRACK_TYPE_BUFFER));
}

static uint8_t ui_core_runtime_bridge_transport_play_command(const ui_event_t *ev)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS) || (ev->id != (uint8_t)BTN_PLAY))
    {
        return 0U;
    }

    seq_runtime_toggle_play_stop();
    return 1U;
}

static uint8_t ui_core_runtime_bridge_transport_pattern_shortcut(const ui_event_t *ev,
                                                                 uint8_t shift_down,
                                                                 uint8_t track_select_armed,
                                                                 ui_core_runtime_bridge_pattern_enter_fn pattern_enter)
{
    if ((ev == 0)
        || (ev->type != UI_EVENT_BUTTON_PRESS)
        || (ev->id != (uint8_t)BTN_TRANSPOSE_DOWN))
    {
        return 0U;
    }

    if ((shift_down == 0U) && (track_select_armed == 0U))
    {
        return 0U;
    }

    if (pattern_enter == 0)
    {
        return 1U;
    }

    pattern_enter((shift_down != 0U) ? UI_PATTERN_MODE_RECALL : UI_PATTERN_MODE_STORE);
    return 1U;
}

static uint8_t ui_core_runtime_bridge_transport_rec_command(const ui_event_t *ev,
                                                            uint8_t shift_down,
                                                            uint8_t track_select_armed,
                                                            ui_core_runtime_bridge_feedback_fn feedback)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS) || (ev->id != (uint8_t)BTN_REC))
    {
        return 0U;
    }

    uint8_t master_buffer_track = 0U;
    const uint8_t has_master_buffer = ui_core_runtime_bridge_find_unique_master_buffer_track(&master_buffer_track);

    if ((track_select_armed != 0U) && (has_master_buffer != 0U))
    {
        if (shift_down != 0U)
        {
            brick6_master_buffer_request_clear();
            if (feedback != 0)
            {
                feedback("BUF CLR");
            }
        }
        else
        {
            brick6_master_buffer_request_record();
            if (feedback != 0)
            {
                if (brick6_master_buffer_is_recording() != 0U)
                {
                    feedback("BUF REC");
                }
                else if (brick6_master_buffer_is_armed() != 0U)
                {
                    feedback("BUF ARM");
                }
                else
                {
                    feedback("BUF STOP");
                }
            }
        }
        return 1U;
    }

    if (shift_down != 0U)
    {
        ui_core_navigation_bridge_open_rec_cfg_page();
        return 1U;
    }

    seq_runtime_set_pattern_rec_target_track(ui_get_active_track());
    seq_runtime_rec_toggle_arm();
    return 1U;
}

static uint8_t ui_core_runtime_bridge_find_unique_master_buffer_track(uint8_t *out_track)
{
    uint8_t found = 0U;
    uint8_t found_track = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if ((ui_get_track_family(track) == UI_TRACK_FAMILY_MASTER)
            && (ui_get_track_type(track) == UI_TRACK_TYPE_BUFFER))
        {
            if (found != 0U)
            {
                return 0U;
            }
            found = 1U;
            found_track = track;
        }
    }

    if (found == 0U)
    {
        return 0U;
    }

    if (out_track != 0)
    {
        *out_track = found_track;
    }
    return 1U;
}

static void ui_core_runtime_bridge_prepare_track_transition_request(ui_system_sync_request_t *request,
                                                                    uint8_t active_track_touched)
{
    if (request == 0)
    {
        return;
    }

    *request = ui_system_sync_make_request_restore_bulk();
    request->notify_keyboard_after_runtime_sync = active_track_touched;
}

static void ui_core_runtime_bridge_init_track_transition_ctx(ui_core_runtime_bridge_track_transition_ctx_t *ctx,
                                                             const ui_system_sync_request_t *request,
                                                             uint8_t sync_active_track_ui_context,
                                                             uint8_t track,
                                                             ui_core_runtime_bridge_post_sync_fn post_sync)
{
    if (ctx == 0)
    {
        return;
    }

    *ctx = (ui_core_runtime_bridge_track_transition_ctx_t){
        .request = request,
        .sync_active_track_ui_context = sync_active_track_ui_context,
        .track = track,
        .post_sync = post_sync
    };
}

static void ui_core_runtime_bridge_init_bulk_track_transition_ctx(ui_core_runtime_bridge_track_transition_ctx_t *ctx,
                                                                  const ui_system_sync_request_t *request,
                                                                  const uint8_t family[UI_TRACK_COUNT],
                                                                  const uint8_t type[UI_TRACK_COUNT],
                                                                  const uint8_t midi_channel[UI_TRACK_COUNT],
                                                                  const uint8_t midi_source[UI_TRACK_COUNT],
                                                                  ui_core_runtime_bridge_post_sync_fn post_sync)
{
    if (ctx == 0)
    {
        return;
    }

    *ctx = (ui_core_runtime_bridge_track_transition_ctx_t){
        .request = request,
        .sync_active_track_ui_context = 1U,
        .family_data = family,
        .type_data = type,
        .midi_channel_data = midi_channel,
        .midi_source_data = midi_source,
        .post_sync = post_sync
    };
}

static void ui_core_runtime_bridge_sync_audio_runtime_enables(void)
{
#if UI_AUDIO_INPUT_PROTO_WIRED_COUNT > UI_AUDIO_INPUT_RESOURCE_COUNT
#error "UI proto wired input count cannot exceed product input resource count"
#endif

    track_enable(0U, track_state_count_tracks_with_family(UI_TRACK_FAMILY_INPUT1) > 0U);
    track_enable(1U, track_state_count_tracks_with_family(UI_TRACK_FAMILY_INPUT2) > 0U);
    track_enable(2U, track_state_count_tracks_with_family(UI_TRACK_FAMILY_INPUT3) > 0U);
    const uint8_t has_engine_track = (uint8_t)((track_state_count_tracks_with_family(UI_TRACK_FAMILY_SYNTH) > 0U)
            || (track_state_count_tracks_with_family(UI_TRACK_FAMILY_DRUM) > 0U));
    track_enable(3U, has_engine_track);
}

static void ui_core_runtime_bridge_notify_keyboard_active_track_changed(void)
{
    if (param_registry_track_structure_transition_is_active() != 0U)
    {
        return;
    }

    keyboard_runtime_sync_track_focus_context();
}

static void ui_core_runtime_bridge_invalidate_runtime_all(void)
{
    track_runtime_invalidate_all();
}

static const ui_system_sync_adapter_t g_ui_core_runtime_bridge_system_sync_adapter = {
    .notify_keyboard_active_track_changed = ui_core_runtime_bridge_notify_keyboard_active_track_changed,
    .invalidate_runtime_all = ui_core_runtime_bridge_invalidate_runtime_all,
    .sync_audio_runtime_enables = ui_core_runtime_bridge_sync_audio_runtime_enables
};

static uint8_t ui_core_runtime_bridge_track_transition_ui_sync_apply(void *ctx_ptr)
{
    const ui_core_runtime_bridge_track_transition_ctx_t *const ctx =
        (const ui_core_runtime_bridge_track_transition_ctx_t *)ctx_ptr;
    if (ctx == 0)
    {
        return 0U;
    }

    mod_lfo_v1_invalidate_dest_cache_all();
    if (ctx->post_sync != 0)
    {
        ctx->post_sync(ctx->sync_active_track_ui_context);
    }
    return 1U;
}

static uint8_t ui_core_runtime_bridge_track_transition_apply_system_sync(
    const ui_core_runtime_bridge_track_transition_ctx_t *ctx)
{
    if ((ctx == 0) || (ctx->request == 0))
    {
        return 0U;
    }

    ui_system_sync_apply_track_context_change(ctx->request, &g_ui_core_runtime_bridge_system_sync_adapter);
    return 1U;
}

static uint8_t ui_core_runtime_bridge_track_family_change_mutate(void *ctx_ptr)
{
    ui_core_runtime_bridge_track_transition_ctx_t *const ctx =
        (ui_core_runtime_bridge_track_transition_ctx_t *)ctx_ptr;
    if (ctx == 0)
    {
        return 0U;
    }

    if (track_state_set_track_family(ctx->track, ctx->family) == false)
    {
        return 0U;
    }

    ui_system_sync_apply_track_context_change(ctx->request, &g_ui_core_runtime_bridge_system_sync_adapter);
    return 1U;
}

static uint8_t ui_core_runtime_bridge_track_type_change_mutate(void *ctx_ptr)
{
    ui_core_runtime_bridge_track_transition_ctx_t *const ctx =
        (ui_core_runtime_bridge_track_transition_ctx_t *)ctx_ptr;
    if (ctx == 0)
    {
        return 0U;
    }

    if (track_state_set_track_type(ctx->track, ctx->type) == false)
    {
        return 0U;
    }

    ui_system_sync_apply_track_context_change(ctx->request, &g_ui_core_runtime_bridge_system_sync_adapter);
    return 1U;
}

static uint8_t ui_core_runtime_bridge_apply_bulk_mutation(const uint8_t family[UI_TRACK_COUNT],
                                                           const uint8_t type[UI_TRACK_COUNT],
                                                           const uint8_t midi_channel[UI_TRACK_COUNT],
                                                           const uint8_t midi_source[UI_TRACK_COUNT])
{
    if ((family == 0) || (type == 0) || (midi_channel == 0) || (midi_source == 0))
    {
        return 0U;
    }

    return track_state_apply_bulk(family, type, midi_channel, midi_source) ? 1U : 0U;
}

static uint8_t ui_core_runtime_bridge_track_transition_mutate_bulk_restore(void *ctx_ptr)
{
    ui_core_runtime_bridge_track_transition_ctx_t *const ctx =
        (ui_core_runtime_bridge_track_transition_ctx_t *)ctx_ptr;
    if ((ctx == 0)
            || (ctx->family_data == 0)
            || (ctx->type_data == 0)
            || (ctx->midi_channel_data == 0)
            || (ctx->midi_source_data == 0))
    {
        return 0U;
    }

    if (ui_core_runtime_bridge_apply_bulk_mutation(ctx->family_data,
                                                   ctx->type_data,
                                                   ctx->midi_channel_data,
                                                   ctx->midi_source_data) == 0U)
    {
        return 0U;
    }

    return ui_core_runtime_bridge_track_transition_apply_system_sync(ctx);
}

static uint8_t ui_core_runtime_bridge_run_track_transition_pipeline(
    param_registry_track_transition_stage_fn_t mutate_fn,
    void *ctx_ptr)
{
    if ((mutate_fn == 0) || (ctx_ptr == 0))
    {
        return 0U;
    }

    const param_registry_track_transition_pipeline_cmd_t transition_cmd = {
        .prepare_fn = NULL,
        .mutate_fn = mutate_fn,
        .reapply_fn = NULL,
        .seq_runtime_sync_fn = NULL,
        .ui_sync_fn = ui_core_runtime_bridge_track_transition_ui_sync_apply,
        .resume_fn = NULL,
        .ctx = ctx_ptr
    };

    return param_registry_run_track_transition_pipeline(&transition_cmd);
}

bool ui_core_runtime_bridge_apply_track_family_change(uint8_t track,
                                                      ui_track_family_t family,
                                                      uint8_t active_track_touched,
                                                      ui_core_runtime_bridge_post_sync_fn post_sync)
{
    ui_system_sync_request_t request;
    ui_core_runtime_bridge_track_transition_ctx_t transition_ctx;
    ui_core_runtime_bridge_prepare_track_transition_request(&request, active_track_touched);
    ui_core_runtime_bridge_init_track_transition_ctx(&transition_ctx,
                                                     &request,
                                                     active_track_touched,
                                                     track,
                                                     post_sync);
    transition_ctx.family = family;

    if (ui_core_runtime_bridge_run_track_transition_pipeline(ui_core_runtime_bridge_track_family_change_mutate,
                                                              (void *)&transition_ctx) == 0U)
    {
        return false;
    }

    return true;
}

bool ui_core_runtime_bridge_apply_track_type_change(uint8_t track,
                                                    ui_track_type_t type,
                                                    uint8_t active_track_touched,
                                                    ui_core_runtime_bridge_post_sync_fn post_sync)
{
    ui_system_sync_request_t request;
    ui_core_runtime_bridge_track_transition_ctx_t transition_ctx;
    ui_core_runtime_bridge_prepare_track_transition_request(&request, active_track_touched);
    ui_core_runtime_bridge_init_track_transition_ctx(&transition_ctx,
                                                     &request,
                                                     active_track_touched,
                                                     track,
                                                     post_sync);
    transition_ctx.type = type;

    if (ui_core_runtime_bridge_run_track_transition_pipeline(ui_core_runtime_bridge_track_type_change_mutate,
                                                              (void *)&transition_ctx) == 0U)
    {
        return false;
    }

    return true;
}

bool ui_core_runtime_bridge_restore_track_config_bulk(const uint8_t family[UI_TRACK_COUNT],
                                                      const uint8_t type[UI_TRACK_COUNT],
                                                      const uint8_t midi_channel[UI_TRACK_COUNT],
                                                      const uint8_t midi_source[UI_TRACK_COUNT],
                                                      ui_core_runtime_bridge_post_sync_fn post_sync)
{
    ui_system_sync_request_t request;
    ui_core_runtime_bridge_track_transition_ctx_t transition_ctx;
    ui_core_runtime_bridge_prepare_track_transition_request(&request, 1U);
    ui_core_runtime_bridge_init_bulk_track_transition_ctx(&transition_ctx,
                                                          &request,
                                                          family,
                                                          type,
                                                          midi_channel,
                                                          midi_source,
                                                          post_sync);

    if (ui_core_runtime_bridge_run_track_transition_pipeline(ui_core_runtime_bridge_track_transition_mutate_bulk_restore,
                                                              (void *)&transition_ctx) == 0U)
    {
        return false;
    }

    return true;
}

uint8_t ui_core_runtime_bridge_handle_master_buffer_routing_event(const ui_event_t *ev,
                                                                  uint8_t active_track,
                                                                  ui_hall_mode_t hall_mode,
                                                                  uint8_t track_select_armed,
                                                                  ui_core_runtime_bridge_suppress_hall_note_fn suppress_hall_note)
{
    const uint8_t is_master_buffer = ui_core_runtime_bridge_track_is_master_buffer(active_track);

    if ((ev == 0)
            || (is_master_buffer == 0U)
            || (hall_mode != UI_HALL_MODE_ARP)
            || (track_select_armed != 0U)
            || (ev->type != UI_EVENT_HALL_PRESS)
            || (ev->id >= UI_TRACK_COUNT))
    {
        return 0U;
    }

    const uint8_t hall = (uint8_t)ev->id;
    const uint8_t enabled = brick6_master_buffer_get_source_enabled(hall);
    brick6_master_buffer_set_source_enabled(hall, (enabled == 0U) ? 1U : 0U);
    if (suppress_hall_note != 0)
    {
        suppress_hall_note(hall);
    }
    return 1U;
}

uint8_t ui_core_runtime_bridge_handle_transport_event(const ui_event_t *ev,
                                                      uint8_t mute_active,
                                                      uint8_t shift_down,
                                                      uint8_t track_select_armed,
                                                      ui_core_runtime_bridge_pattern_enter_fn pattern_enter,
                                                      ui_core_runtime_bridge_feedback_fn feedback)
{
    (void)mute_active;

    if (ui_core_runtime_bridge_transport_play_command(ev) != 0U)
    {
        return 1U;
    }

    if (ui_core_runtime_bridge_transport_rec_command(ev, shift_down, track_select_armed, feedback) != 0U)
    {
        return 1U;
    }

    return ui_core_runtime_bridge_transport_pattern_shortcut(ev,
                                                             shift_down,
                                                             track_select_armed,
                                                             pattern_enter);
}

uint8_t ui_core_runtime_bridge_request_undo(ui_core_runtime_bridge_feedback_fn feedback)
{
    const uint8_t ok = (undo_v2_undo() == UNDO_V2_STATUS_OK) ? 1U : 0U;
    if (ok != 0U)
    {
        if (feedback != 0)
        {
            feedback("UNDO");
        }
    }
    else if (feedback != 0)
    {
        feedback("UNDO N/A");
    }

    return ok;
}

uint8_t ui_core_runtime_bridge_handle_pattern_mode_event(const ui_event_t *ev,
                                                         ui_hall_mode_t hall_mode,
                                                         uint8_t shift_down,
                                                         uint8_t track_select_armed,
                                                         ui_core_runtime_bridge_set_hall_mode_fn set_hall_mode,
                                                         ui_core_runtime_bridge_feedback_fn feedback)
{
    return ui_core_pattern_handle_mode_event(ev,
                                             hall_mode,
                                             shift_down,
                                             track_select_armed,
                                             set_hall_mode,
                                             feedback);
}

uint8_t ui_core_runtime_bridge_handle_global_shortcuts(const ui_event_t *ev,
                                                       uint8_t shift_down,
                                                       uint8_t track_select_armed,
                                                       uint8_t mute_active,
                                                       ui_core_runtime_bridge_undo_request_fn undo_request,
                                                       ui_core_runtime_bridge_feedback_fn feedback)
{
    return ui_core_shortcuts_handle_global_event(ev,
                                                 shift_down,
                                                 track_select_armed,
                                                 mute_active,
                                                 undo_request,
                                                 feedback);
}

uint8_t ui_core_runtime_bridge_handle_seq_mode_event(const ui_event_t *ev,
                                                     ui_hall_mode_t hall_mode,
                                                     uint8_t shift_down,
                                                     ui_core_runtime_bridge_feedback_fn feedback)
{
    return ui_core_seq_transport_handle_seq_mode_event(ev,
                                                       hall_mode,
                                                       shift_down,
                                                       feedback);
}

void ui_core_runtime_bridge_step_octave(int8_t step)
{
    keyboard_runtime_step_octave(step);
}

void ui_core_runtime_bridge_notify_hall_mode_changed(ui_hall_mode_t previous_mode,
                                                     ui_hall_mode_t next_mode)
{
    ui_macro_interaction_reset();
    keyboard_runtime_on_hall_mode_changed(previous_mode, next_mode);
}

void ui_core_runtime_bridge_update_seq_step_hold(void)
{
    seq_edit_step_hold_update();
}

uint8_t ui_core_runtime_bridge_resolve_filter_target_track(uint8_t *out_track_id)
{
    track_runtime_resolved_track_t resolved;
    if ((out_track_id == 0)
            || (track_runtime_resolve_track(ui_get_active_track(), &resolved) == 0U)
            || (resolved.has_filter_target == 0U))
    {
        return 0U;
    }

    *out_track_id = resolved.filter_track_id;
    return 1U;
}

uint8_t ui_core_runtime_bridge_get_seq_edit_page(uint8_t track)
{
    return seq_edit_get_page(track);
}

int8_t ui_core_runtime_bridge_get_keyboard_octave_shift(void)
{
    return keyboard_runtime_get_octave_shift();
}

void ui_core_runtime_bridge_get_pattern_stub_state(ui_pattern_stub_state_t *out_state)
{
    if (out_state == 0)
    {
        return;
    }

    uint8_t active_bank = 0U;
    uint8_t active_pattern = 0U;
    uint8_t queued_valid = 0U;
    uint8_t queued_bank = 0U;
    uint8_t queued_pattern = 0U;

    (void)pattern_live_get_active(&active_bank, &active_pattern);
    (void)pattern_live_get_queued(&queued_valid, &queued_bank, &queued_pattern);

    out_state->active_bank = active_bank;
    out_state->active_pattern = active_pattern;
    out_state->queued_valid = queued_valid;
    out_state->queued_bank = queued_bank;
    out_state->queued_pattern = queued_pattern;
    out_state->substate = ui_core_pattern_get_substate();
    out_state->selected_bank = ui_core_pattern_get_selected_bank();
    out_state->mode = ui_core_pattern_get_mode();
}

void ui_core_runtime_bridge_sync_active_track_context(uint8_t include_keyboard_focus_sync)
{
    ui_active_track_sync_mirror();
    ui_edit_context_sync_active_track(include_keyboard_focus_sync);
}

void ui_core_runtime_bridge_sync_active_track_mirror(void)
{
    ui_active_track_sync_mirror();
}

void ui_core_runtime_bridge_sync_active_track_midi_channel(void)
{
    ui_active_track_sync_mirror_cfg_midi_channel();
}

void ui_core_runtime_bridge_sync_active_track_midi_source(void)
{
    ui_active_track_sync_mirror_cfg_midi_source();
}

void ui_core_runtime_bridge_post_track_structure_change(uint8_t sync_active_track_ui_context)
{
    ui_active_track_sync_after_track_structure_change(sync_active_track_ui_context);
}
