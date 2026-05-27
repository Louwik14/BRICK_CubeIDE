#include "ui_core_runtime_bridge.h"

#include "App/Hall/hall_engine.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Keyboard/keyboard_runtime.h"
#include "Mod/mod_lfo_v1.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_runtime_exec.h"
#include "Storage/looper_storage.h"
#include "Storage/memory_layout.h"
#include "Storage/multi_record_writer.h"
#include "Storage/sd_preview.h"
#include "Storage/undo_v2.h"
#include "Storage/wav_loader.h"
#include "Storage/waveform_cache.h"
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

#include <stdio.h>
#include <string.h>

#define UI_LOOPER_RECORD_CLIENT_ID 0U
#define UI_LOOPER_LEN_STEPS_PER_BAR 16U
#define UI_LOOPER_LEN_UNLIMITED_STEPS 0U

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

static uint8_t g_master_fx_route_enabled[UI_TRACK_COUNT];
static uint8_t g_looper_route_enabled[UI_TRACK_COUNT][UI_TRACK_COUNT];
static uint8_t g_active_looper_record_track = 0xFFU;
static uint8_t g_looper_take_track = 0xFFU;
static uint8_t g_looper_record_auto_stop_latched = 0U;
static uint8_t g_looper_take_notified = 0U;
static uint8_t g_looper_transport_was_running = 0U;
static uint8_t g_looper_transport_start_prearmed = 0U;
static uint8_t g_looper_export_last_progress = 0xFFU;
static looper_storage_raw_export_phase_t g_looper_export_last_phase =
    LOOPER_STORAGE_RAW_EXPORT_PHASE_IDLE;
UI_STATE_SDRAM static ui_core_runtime_bridge_looper_save_diag_t g_looper_save_diag;

static uint8_t ui_core_runtime_bridge_looper_record_is_active(void);
static uint8_t ui_core_runtime_bridge_looper_play_is_auto(uint8_t track);
static uint8_t ui_core_runtime_bridge_looper_handle_stop(ui_core_runtime_bridge_feedback_fn feedback);
static uint8_t ui_core_runtime_bridge_looper_resolve_raw_slot(uint8_t track, uint8_t *out_raw_slot);
static const char *ui_core_runtime_bridge_looper_raw_feedback(uint8_t slot_failed);
static uint8_t ui_core_runtime_bridge_looper_prepare_record_pre_transport_start(
    ui_core_runtime_bridge_feedback_fn feedback);
static uint8_t ui_core_runtime_bridge_looper_start_track(uint8_t track,
                                                         ui_core_runtime_bridge_feedback_fn feedback);
static uint8_t ui_core_runtime_bridge_looper_handle_save(ui_core_runtime_bridge_feedback_fn feedback);
static void ui_core_runtime_bridge_looper_copy_diag_path(char *dst, const char *src);
static uint8_t ui_core_runtime_bridge_looper_snapshot_matches(
    const brick6_looper_runtime_diag_snapshot_t *before,
    const brick6_looper_runtime_diag_snapshot_t *after);
static void ui_core_runtime_bridge_prepare_restore_transition_request(ui_system_sync_request_t *request,
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

void ui_core_runtime_bridge_init(void)
{
    memset(&g_looper_save_diag, 0, sizeof(g_looper_save_diag));
}

static void ui_core_runtime_bridge_looper_copy_diag_path(char *dst, const char *src)
{
    if(dst == 0)
    {
        return;
    }

    dst[0] = '\0';
    if(src == 0)
    {
        return;
    }

    for(uint32_t i = 0U; i < BRICK6_LOOPER_RUNTIME_DIAG_PATH_MAX; ++i)
    {
        dst[i] = src[i];
        if(src[i] == '\0')
        {
            return;
        }
    }
    dst[BRICK6_LOOPER_RUNTIME_DIAG_PATH_MAX - 1U] = '\0';
}

static uint8_t ui_core_runtime_bridge_looper_snapshot_matches(
    const brick6_looper_runtime_diag_snapshot_t *before,
    const brick6_looper_runtime_diag_snapshot_t *after)
{
    if((before == 0) || (after == 0))
    {
        return 0U;
    }

    return (uint8_t)((before->playhead == after->playhead)
            && (before->recorded_frames == after->recorded_frames)
            && (before->scheduled_start_sample == after->scheduled_start_sample)
            && (before->start_playhead == after->start_playhead)
            && (before->cache_id == after->cache_id)
            && (before->raw_slot == after->raw_slot)
            && (before->source == after->source)
            && (before->state == after->state)
            && (before->cache_registered == after->cache_registered)
            && (strncmp(before->active_path,
                        after->active_path,
                        BRICK6_LOOPER_RUNTIME_DIAG_PATH_MAX) == 0));
}

static uint8_t ui_core_runtime_bridge_track_is_sampler_looper(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 0U;
    }

    return (uint8_t)((track_state_get_family(track) == UI_TRACK_FAMILY_SAMPLER)
            && (track_state_get_type(track) == UI_TRACK_TYPE_LOOPER));
}

static uint8_t ui_core_runtime_bridge_track_is_master_fx(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 0U;
    }

    return (uint8_t)((track_state_get_family(track) == UI_TRACK_FAMILY_MASTER)
            && (track_state_get_type(track) == UI_TRACK_TYPE_MASTER_FX));
}

static uint8_t ui_core_runtime_bridge_transport_play_command(const ui_event_t *ev,
                                                             uint8_t shift_down,
                                                             uint8_t track_select_armed,
                                                             ui_core_runtime_bridge_feedback_fn feedback)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS) || (ev->id != (uint8_t)BTN_PLAY))
    {
        return 0U;
    }

    (void)shift_down;
    (void)track_select_armed;
    if ((seq_runtime_is_running() == 0U) && (seq_runtime_is_start_pending() == 0U))
    {
        (void)ui_core_runtime_bridge_looper_prepare_record_pre_transport_start(feedback);
        brick6_looper_runtime_on_transport_start();
        g_looper_transport_was_running = 1U;
        g_looper_transport_start_prearmed = 1U;
    }
    seq_runtime_toggle_play_stop();
    ui_core_runtime_bridge_service_looper_record_control(feedback);
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

    (void)track_select_armed;
    if (shift_down != 0U)
    {
        ui_core_navigation_bridge_open_rec_cfg_page();
        return 1U;
    }

    uint8_t rec_target_track = ui_get_active_track();
    (void)track_runtime_get_voice_group_effective_master(rec_target_track, &rec_target_track);
    seq_runtime_set_pattern_rec_target_track(rec_target_track);
    seq_runtime_rec_toggle_arm();
    ui_core_runtime_bridge_service_looper_record_control(feedback);
    return 1U;
}

static uint8_t ui_core_runtime_bridge_looper_state_is_active(multi_record_writer_state_t state)
{
    return (uint8_t)((state == MULTI_RECORD_WRITER_STATE_RECORDING)
        || (state == MULTI_RECORD_WRITER_STATE_STOP_REQUESTED)
        || (state == MULTI_RECORD_WRITER_STATE_DRAINING)
        || (state == MULTI_RECORD_WRITER_STATE_FINALIZING));
}

static uint32_t ui_core_runtime_bridge_looper_len_to_steps(float len)
{
    const uint8_t len_index = (uint8_t)(len + 0.5f);
    uint32_t bars = 0U;

    switch (len_index)
    {
        case 1U:
            bars = 1U;
            break;
        case 2U:
            bars = 2U;
            break;
        case 3U:
            bars = 4U;
            break;
        case 4U:
            bars = 8U;
            break;
        case 5U:
            bars = 16U;
            break;
        default:
            return UI_LOOPER_LEN_UNLIMITED_STEPS;
    }

    return bars * UI_LOOPER_LEN_STEPS_PER_BAR;
}

static uint32_t ui_core_runtime_bridge_looper_track_len_steps(uint8_t track)
{
    float len = 0.0f;
    if (param_registry_get_track_value(PARAM_LOOPER_LEN, track, &len) == 0U)
    {
        return UI_LOOPER_LEN_UNLIMITED_STEPS;
    }

    return ui_core_runtime_bridge_looper_len_to_steps(len);
}

static uint8_t ui_core_runtime_bridge_looper_len_mode(uint8_t track)
{
    float len = 0.0f;
    if (param_registry_get_track_value(PARAM_LOOPER_LEN, track, &len) == 0U)
    {
        return 0U;
    }

    return (uint8_t)(len + 0.5f);
}

static uint32_t ui_core_runtime_bridge_looper_expected_record_frames(uint8_t track)
{
    const uint32_t target_steps = ui_core_runtime_bridge_looper_track_len_steps(track);
    if (target_steps == UI_LOOPER_LEN_UNLIMITED_STEPS)
    {
        return 0U;
    }

    const uint32_t samples_per_step_q16 = seq_runtime_get_samples_per_step_q16();
    if (samples_per_step_q16 == 0U)
    {
        return 0U;
    }

    const uint64_t frames_q16 = (uint64_t)target_steps * (uint64_t)samples_per_step_q16;
    uint64_t frames = (frames_q16 + 0xFFFFULL) >> 16;
    if (frames > 0xFFFFFFFFULL)
    {
        frames = 0xFFFFFFFFULL;
    }
    return (uint32_t)frames;
}

static uint8_t ui_core_runtime_bridge_looper_play_is_auto(uint8_t track)
{
    float play = 0.0f;
    return (uint8_t)(((param_registry_get_track_value(PARAM_LOOPER_PLAY, track, &play) != 0U)
            && ((uint8_t)(play + 0.5f) == 1U)) ? 1U : 0U);
}

static uint8_t ui_core_runtime_bridge_looper_record_is_active(void)
{
    if(brick6_looper_runtime_record_is_active_or_armed() != 0U)
    {
        return 1U;
    }

    multi_record_writer_status_t status;
    if (multi_record_writer_get_status(UI_LOOPER_RECORD_CLIENT_ID, &status) == 0U)
    {
        return 0U;
    }

    return ui_core_runtime_bridge_looper_state_is_active(status.state);
}

static uint8_t ui_core_runtime_bridge_looper_track_has_route(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 0U;
    }

    for (uint8_t source = 0U; source < UI_TRACK_COUNT; ++source)
    {
        if ((source != track) && (g_looper_route_enabled[track][source] != 0U))
        {
            return 1U;
        }
    }

    return 0U;
}

static void ui_core_runtime_bridge_looper_clear_take_metadata(multi_record_writer_state_t state)
{
    (void)state;
    g_looper_take_track = 0xFFU;
    g_looper_take_notified = 0U;
}

static uint8_t ui_core_runtime_bridge_looper_handle_stop(ui_core_runtime_bridge_feedback_fn feedback)
{
    if (ui_core_runtime_bridge_looper_record_is_active() == 0U)
    {
        return 0U;
    }

    brick6_looper_runtime_arm_record_stop(seq_runtime_exec_get_audio_timeline_sample());
    if (brick6_looper_runtime_record_is_active_or_armed() != 0U)
    {
        if (feedback != 0)
        {
            feedback("LOOP STOP");
        }
        return 1U;
    }

    multi_record_writer_status_t status;
    if((multi_record_writer_get_status(UI_LOOPER_RECORD_CLIENT_ID, &status) != 0U)
            && (ui_core_runtime_bridge_looper_state_is_active(status.state) != 0U))
    {
        if (feedback != 0)
        {
            feedback("LOOP STOP");
        }
        return 1U;
    }

    if (feedback != 0)
    {
        feedback("LOOP ERR");
    }
    return 1U;
}

static uint8_t ui_core_runtime_bridge_looper_handle_save(ui_core_runtime_bridge_feedback_fn feedback)
{
    const uint8_t track = ui_get_active_track();
    if (ui_core_runtime_bridge_track_is_sampler_looper(track) == 0U)
    {
        return 0U;
    }

    if (looper_storage_raw_export_is_active() != 0U)
    {
        if (feedback != 0)
        {
            char msg[16];
            (void)snprintf(msg,
                           sizeof(msg),
                           "SAVE %u%%",
                           (unsigned)looper_storage_raw_export_get_progress_percent());
            feedback(msg);
        }
        return 1U;
    }

    if (seq_runtime_is_running() != 0U)
    {
        if (feedback != 0)
        {
            feedback("STOP SAVE");
        }
        return 1U;
    }

    if (sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    multi_record_writer_status_t status;
    if (multi_record_writer_get_status(UI_LOOPER_RECORD_CLIENT_ID, &status) == 0U)
    {
        if (feedback != 0)
        {
            feedback("LOOP FAIL");
        }
        return 1U;
    }

    if (ui_core_runtime_bridge_looper_state_is_active(status.state) != 0U)
    {
        if (feedback != 0)
        {
            feedback("LOOP BUSY");
        }
        return 1U;
    }

    if ((status.state == MULTI_RECORD_WRITER_STATE_TAKE_READY)
        && (status.frames_written != 0U)
        && (status.raw_slot != MULTI_RECORD_WRITER_RAW_SLOT_NONE)
        && (g_looper_take_track == track))
    {
        uint8_t raw_slot = MULTI_RECORD_WRITER_RAW_SLOT_NONE;
        const char *raw_path = 0;
        uint32_t recorded_frames = 0U;
        if (multi_record_writer_get_last_raw_take(UI_LOOPER_RECORD_CLIENT_ID,
                                                  &raw_slot,
                                                  &raw_path,
                                                  &recorded_frames) == 0U)
        {
            if (feedback != 0)
            {
                feedback("NO LOOP");
            }
            return 1U;
        }

        char final_path[MULTI_RECORD_WRITER_PATH_MAX];
        const looper_storage_path_result_t path_result =
            looper_storage_make_next_path(track, final_path, sizeof(final_path));
        if (path_result == LOOPER_STORAGE_PATH_BUSY)
        {
            if (feedback != 0)
            {
                feedback("LOOP BUSY");
            }
            return 1U;
        }
        if (path_result != LOOPER_STORAGE_PATH_OK)
        {
            if (feedback != 0)
            {
                feedback("LOOP FAIL");
            }
            return 1U;
        }

        if (looper_storage_raw_export_start(track,
                                            raw_slot,
                                            raw_path,
                                            recorded_frames,
                                            final_path) == 0U)
        {
            if (feedback != 0)
            {
                feedback("LOOP FAIL");
            }
            return 1U;
        }

        memset(&g_looper_save_diag, 0, sizeof(g_looper_save_diag));
        g_looper_save_diag.recorded_frames = recorded_frames;
        g_looper_save_diag.raw_slot = raw_slot;
        ui_core_runtime_bridge_looper_copy_diag_path(g_looper_save_diag.raw_path, raw_path);
        ui_core_runtime_bridge_looper_copy_diag_path(g_looper_save_diag.wav_path, final_path);
        g_looper_export_last_progress = 0U;
        g_looper_export_last_phase = LOOPER_STORAGE_RAW_EXPORT_PHASE_WAIT;
        if (feedback != 0)
        {
            feedback("SAVE WAIT");
        }
        return 1U;
    }

    if ((status.state != MULTI_RECORD_WRITER_STATE_TAKE_READY)
        || (status.frames_written == 0U)
        || (status.raw_slot != MULTI_RECORD_WRITER_RAW_SLOT_NONE)
        || (g_looper_take_track != track))
    {
        if (feedback != 0)
        {
            feedback("NO LOOP");
        }
        return 1U;
    }

    if (feedback != 0)
    {
        feedback("NO LOOP");
    }
    return 1U;
}

static uint8_t ui_core_runtime_bridge_looper_track_is_record_eligible(uint8_t track)
{
    if (ui_core_runtime_bridge_track_is_sampler_looper(track) == 0U)
    {
        return 0U;
    }

    float arm = 0.0f;
    if (param_registry_get_track_value(PARAM_LOOPER_ARM, track, &arm) == 0U)
    {
        return 0U;
    }

    const uint8_t arm_mode = (uint8_t)(arm + 0.5f);
    if (arm_mode != 1U)
    {
        return 0U;
    }

    if (ui_core_runtime_bridge_looper_track_has_route(track) == 0U)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t ui_core_runtime_bridge_looper_find_single_record_eligible(uint8_t *out_track,
                                                                         uint8_t *out_count)
{
    uint8_t found_track = 0U;
    uint8_t count = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if (ui_core_runtime_bridge_looper_track_is_record_eligible(track) != 0U)
        {
            if (count == 0U)
            {
                found_track = track;
            }
            count++;
        }
    }

    if (out_count != 0)
    {
        *out_count = count;
    }
    if ((out_track != 0) && (count == 1U))
    {
        *out_track = found_track;
    }
    return (count == 1U) ? 1U : 0U;
}

static uint8_t ui_core_runtime_bridge_looper_prepare_record_pre_transport_start(
    ui_core_runtime_bridge_feedback_fn feedback)
{
    if (seq_runtime_rec_is_armed() == 0U)
    {
        return 0U;
    }

    if (ui_core_runtime_bridge_looper_record_is_active() != 0U)
    {
        return 1U;
    }

    if (g_looper_record_auto_stop_latched != 0U)
    {
        return 0U;
    }

    if (multi_record_writer_any_active() != 0U)
    {
        if (feedback != 0)
        {
            feedback("REC BUSY");
        }
        return 0U;
    }

    if (looper_storage_raw_export_is_active() != 0U)
    {
        if (feedback != 0)
        {
            feedback("SAVE BUSY");
        }
        return 0U;
    }

    uint8_t eligible_track = 0U;
    uint8_t eligible_count = 0U;
    if (ui_core_runtime_bridge_looper_find_single_record_eligible(&eligible_track, &eligible_count) == 0U)
    {
        if ((eligible_count > 1U) && (feedback != 0))
        {
            feedback("LOOP MULTI");
        }
        return 0U;
    }

    return ui_core_runtime_bridge_looper_start_track(eligible_track, feedback);
}

static uint8_t ui_core_runtime_bridge_looper_resolve_raw_slot(uint8_t track, uint8_t *out_raw_slot)
{
    if ((track >= UI_TRACK_COUNT) || (out_raw_slot == 0))
    {
        return 0U;
    }

    track_runtime_refresh_all();

    if (looper_storage_raw_is_available() == 0U)
    {
        (void)looper_storage_raw_validate();
    }

    if (looper_storage_raw_is_available() == 0U)
    {
        return 0U;
    }

    return looper_storage_raw_get_slot_for_track(track, out_raw_slot);
}

static const char *ui_core_runtime_bridge_looper_raw_feedback(uint8_t slot_failed)
{
    if (slot_failed != 0U)
    {
        return "RAW SLOT";
    }

    switch (looper_storage_raw_get_last_error())
    {
        case LOOPER_STORAGE_RAW_ERROR_SD_BUSY:
            return "RAW BUSY";
        case LOOPER_STORAGE_RAW_ERROR_MOUNT_FAIL:
            return "RAW MOUNT";
        case LOOPER_STORAGE_RAW_ERROR_MISSING:
            return "RAW MISS";
        case LOOPER_STORAGE_RAW_ERROR_STAT_FAIL:
            return "RAW STAT";
        case LOOPER_STORAGE_RAW_ERROR_SIZE_MISMATCH:
            return "RAW SIZE";
        case LOOPER_STORAGE_RAW_ERROR_NOT_VALIDATED:
            return "RAW INIT";
        case LOOPER_STORAGE_RAW_ERROR_NONE:
        default:
            return "LOOP RAW";
    }
}

static uint8_t ui_core_runtime_bridge_looper_start_track(uint8_t track,
                                                         ui_core_runtime_bridge_feedback_fn feedback)
{
    multi_record_writer_status_t status;
    if (multi_record_writer_get_status(UI_LOOPER_RECORD_CLIENT_ID, &status) == 0U)
    {
        if (feedback != 0)
        {
            feedback("LOOP ERR");
        }
        return 1U;
    }
    if(((status.state == MULTI_RECORD_WRITER_STATE_PREPARED)
            || (status.state == MULTI_RECORD_WRITER_STATE_TAKE_READY)
            || (status.state == MULTI_RECORD_WRITER_STATE_FAILED))
            && (g_looper_take_track < UI_TRACK_COUNT))
    {
        brick6_looper_runtime_prepare_replace(g_looper_take_track);
    }
    ui_core_runtime_bridge_looper_clear_take_metadata(status.state);

    uint8_t raw_slot = 0U;
    if (ui_core_runtime_bridge_looper_resolve_raw_slot(track, &raw_slot) == 0U)
    {
        if (feedback != 0)
        {
            const uint8_t slot_failed = (looper_storage_raw_is_available() != 0U) ? 1U : 0U;
            feedback(ui_core_runtime_bridge_looper_raw_feedback(slot_failed));
        }
        return 1U;
    }

    const char *const raw_path = looper_storage_raw_get_path(raw_slot);
    if (raw_path == 0)
    {
        if (feedback != 0)
        {
            feedback("RAW SLOT");
        }
        return 1U;
    }

    const uint32_t expected_frames = ui_core_runtime_bridge_looper_expected_record_frames(track);
    const uint64_t rec_request_sample = seq_runtime_exec_get_audio_timeline_sample();
    if (multi_record_writer_prepare_raw(UI_LOOPER_RECORD_CLIENT_ID,
                                        raw_slot,
                                        raw_path,
                                        expected_frames) == 0U)
    {
        if (feedback != 0)
        {
            feedback("LOOP SD");
        }
        return 1U;
    }

    brick6_looper_runtime_prepare_replace(track);
    g_active_looper_record_track = track;
    g_looper_take_track = track;
    g_looper_take_notified = 0U;
    brick6_looper_runtime_arm_record_start(track,
                                           raw_slot,
                                           ui_core_runtime_bridge_looper_len_mode(track),
                                           expected_frames,
                                           ui_core_runtime_bridge_looper_play_is_auto(track),
                                           rec_request_sample);
    (void)param_registry_apply_track_value(PARAM_LOOPER_ARM, track, 0.0f);
    if (feedback != 0)
    {
        feedback("LOOP REC");
    }
    return 1U;
}

void ui_core_runtime_bridge_service_looper_record_control(ui_core_runtime_bridge_feedback_fn feedback)
{
    const uint8_t transport_running = seq_runtime_is_running();
    const uint8_t transport_start_pending = seq_runtime_is_start_pending();
    uint8_t keep_prearmed_running_mirror = 0U;
    if ((transport_running != 0U) && (g_looper_transport_was_running == 0U))
    {
        brick6_looper_runtime_on_transport_start();
    }
    else if ((transport_running == 0U) && (g_looper_transport_was_running != 0U))
    {
        if ((g_looper_transport_start_prearmed != 0U) && (transport_start_pending != 0U))
        {
            keep_prearmed_running_mirror = 1U;
        }
        else
        {
            brick6_looper_runtime_on_transport_stop();
            g_looper_transport_start_prearmed = 0U;
        }
    }
    if (transport_running != 0U)
    {
        g_looper_transport_start_prearmed = 0U;
    }
    g_looper_transport_was_running =
        (keep_prearmed_running_mirror != 0U) ? 1U : transport_running;

    multi_record_writer_status_t status;
    if ((multi_record_writer_get_status(UI_LOOPER_RECORD_CLIENT_ID, &status) != 0U)
        && (status.state == MULTI_RECORD_WRITER_STATE_TAKE_READY)
        && (status.error == MULTI_RECORD_WRITER_ERROR_NONE)
        && (status.frames_written != 0U)
        && (g_looper_take_track < UI_TRACK_COUNT)
        && (g_looper_take_notified == 0U))
    {
        const uint8_t play_auto = ui_core_runtime_bridge_looper_play_is_auto(g_looper_take_track);
        if(status.raw_slot != MULTI_RECORD_WRITER_RAW_SLOT_NONE)
        {
            uint8_t raw_slot = MULTI_RECORD_WRITER_RAW_SLOT_NONE;
            const char *raw_path = 0;
            uint32_t recorded_frames = 0U;
            if(multi_record_writer_get_last_raw_take(UI_LOOPER_RECORD_CLIENT_ID,
                                                     &raw_slot,
                                                     &raw_path,
                                                     &recorded_frames) != 0U)
            {
                brick6_looper_runtime_notify_raw_take_ready(g_looper_take_track,
                                                            raw_slot,
                                                            raw_path,
                                                            recorded_frames,
                                                            play_auto,
                                                            0U);
                g_looper_take_notified = 1U;
            }
        }
    }

    if ((((transport_running == 0U)
            && ((transport_start_pending == 0U) || (g_looper_transport_start_prearmed == 0U))))
            || (seq_runtime_rec_is_armed() == 0U))
    {
        (void)ui_core_runtime_bridge_looper_handle_stop(feedback);
        g_looper_record_auto_stop_latched = 0U;
        return;
    }

    if (ui_core_runtime_bridge_looper_record_is_active() != 0U)
    {
        return;
    }

    if (g_looper_record_auto_stop_latched != 0U)
    {
        return;
    }

    if (multi_record_writer_any_active() != 0U)
    {
        if (feedback != 0)
        {
            feedback("REC BUSY");
        }
        return;
    }

    if (looper_storage_raw_export_is_active() != 0U)
    {
        if (feedback != 0)
        {
            feedback("SAVE BUSY");
        }
        return;
    }

    uint8_t eligible_track = 0U;
    uint8_t eligible_count = 0U;
    if (ui_core_runtime_bridge_looper_find_single_record_eligible(&eligible_track, &eligible_count) == 0U)
    {
        if ((eligible_count > 1U) && (feedback != 0))
        {
            feedback("LOOP MULTI");
        }
        return;
    }

    (void)ui_core_runtime_bridge_looper_start_track(eligible_track, feedback);
}

void ui_core_runtime_bridge_service_looper_export_feedback(ui_core_runtime_bridge_feedback_fn feedback)
{
    if (feedback == 0)
    {
        return;
    }

    const looper_storage_raw_export_state_t state = looper_storage_raw_export_get_state();
    if (state == LOOPER_STORAGE_RAW_EXPORT_ACTIVE)
    {
        const looper_storage_raw_export_phase_t phase = looper_storage_raw_export_get_phase();
        const uint8_t progress = looper_storage_raw_export_get_progress_percent();
        if ((phase != g_looper_export_last_phase) || (progress != g_looper_export_last_progress))
        {
            char msg[16];
            g_looper_export_last_progress = progress;
            g_looper_export_last_phase = phase;
            switch (phase)
            {
                case LOOPER_STORAGE_RAW_EXPORT_PHASE_WAIT:
                    (void)snprintf(msg, sizeof(msg), "SAVE WAIT");
                    break;
                case LOOPER_STORAGE_RAW_EXPORT_PHASE_OPEN:
                    (void)snprintf(msg, sizeof(msg), "SAVE OPEN");
                    break;
                case LOOPER_STORAGE_RAW_EXPORT_PHASE_VERIFY:
                    (void)snprintf(msg, sizeof(msg), "SAVE VERIFY");
                    break;
                case LOOPER_STORAGE_RAW_EXPORT_PHASE_WRITE:
                default:
                    (void)snprintf(msg, sizeof(msg), "SAVE %u%%", (unsigned)progress);
                    break;
            }
            feedback(msg);
        }
        return;
    }

    if (state == LOOPER_STORAGE_RAW_EXPORT_DONE)
    {
        brick6_looper_runtime_diag_get_snapshot(&g_looper_save_diag.before_success);
        (void)wav_loader_catalog_notify_file_created(looper_storage_raw_export_get_final_path());
        looper_storage_raw_export_diag_t export_diag;
        looper_storage_raw_export_get_diag(&export_diag);
        (void)waveform_cache_request_for_wav_known_duration(
            looper_storage_raw_export_get_final_path(),
            WAVEFORM_CACHE_REASON_POST_LOOPER_SAVE,
            export_diag.recorded_frames,
            LOOPER_STORAGE_RAW_SAMPLE_RATE_HZ);
        feedback("LOOP SAVED");
        g_looper_export_last_progress = 0xFFU;
        g_looper_export_last_phase = LOOPER_STORAGE_RAW_EXPORT_PHASE_IDLE;
        looper_storage_raw_export_clear_finished();
        brick6_looper_runtime_diag_get_snapshot(&g_looper_save_diag.after_success);
        g_looper_save_diag.valid = 1U;
        g_looper_save_diag.playback_unchanged =
            ui_core_runtime_bridge_looper_snapshot_matches(&g_looper_save_diag.before_success,
                                                           &g_looper_save_diag.after_success);
        return;
    }

    if (state == LOOPER_STORAGE_RAW_EXPORT_FAILED)
    {
        feedback("SAVE FAIL");
        g_looper_export_last_progress = 0xFFU;
        g_looper_export_last_phase = LOOPER_STORAGE_RAW_EXPORT_PHASE_IDLE;
        looper_storage_raw_export_clear_finished();
    }
}

void ui_core_runtime_bridge_get_looper_save_diag(ui_core_runtime_bridge_looper_save_diag_t *out_diag)
{
    if(out_diag == 0)
    {
        return;
    }

    *out_diag = g_looper_save_diag;
}

static void ui_core_runtime_bridge_prepare_restore_transition_request(ui_system_sync_request_t *request,
                                                                      uint8_t active_track_touched)
{
    if (request == 0)
    {
        return;
    }

    *request = ui_system_sync_make_request_restore_bulk();
    request->notify_keyboard_after_runtime_sync = active_track_touched;
}

static void ui_core_runtime_bridge_prepare_track_family_transition_request(ui_system_sync_request_t *request,
                                                                          uint8_t track,
                                                                          uint8_t active_track_touched)
{
    if (request == 0)
    {
        return;
    }

    *request = ui_system_sync_make_request_track_family_change(track, active_track_touched);
}

static void ui_core_runtime_bridge_prepare_track_type_transition_request(ui_system_sync_request_t *request,
                                                                        uint8_t track,
                                                                        uint8_t active_track_touched)
{
    if (request == 0)
    {
        return;
    }

    *request = ui_system_sync_make_request_track_type_change(track, active_track_touched);
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
            || (track_state_count_tracks_with_family(UI_TRACK_FAMILY_SAMPLER) > 0U)
            || (track_state_count_tracks_with_family(UI_TRACK_FAMILY_DRUM) > 0U));
    track_enable(3U, has_engine_track);
}

static void ui_core_runtime_bridge_notify_keyboard_active_track_changed(void)
{
    const uint8_t active_track = ui_get_active_track();
    if ((param_registry_track_structure_transition_is_global_active() != 0U)
            || (param_registry_track_structure_transition_is_track_active(active_track) != 0U))
    {
        return;
    }

    keyboard_runtime_sync_track_focus_context();
}

static void ui_core_runtime_bridge_invalidate_runtime_all(void)
{
    track_runtime_invalidate_all();
}

static void ui_core_runtime_bridge_invalidate_runtime_track(uint8_t track)
{
    track_runtime_invalidate_track(track);
}

static const ui_system_sync_adapter_t g_ui_core_runtime_bridge_system_sync_adapter = {
    .notify_keyboard_active_track_changed = ui_core_runtime_bridge_notify_keyboard_active_track_changed,
    .invalidate_runtime_all = ui_core_runtime_bridge_invalidate_runtime_all,
    .invalidate_runtime_track = ui_core_runtime_bridge_invalidate_runtime_track,
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

    if ((ctx->request != 0)
            && (ctx->request->runtime_track != UI_SYSTEM_SYNC_RUNTIME_TRACK_ALL))
    {
        mod_lfo_v1_invalidate_dest_cache_track(ctx->request->runtime_track);
    }
    else
    {
        mod_lfo_v1_invalidate_dest_cache_all();
    }
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
    void *ctx_ptr,
    uint8_t target_track)
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

    return param_registry_run_track_transition_pipeline_for_track(&transition_cmd, target_track);
}

bool ui_core_runtime_bridge_apply_track_family_change(uint8_t track,
                                                      ui_track_family_t family,
                                                      uint8_t active_track_touched,
                                                      ui_core_runtime_bridge_post_sync_fn post_sync)
{
    ui_system_sync_request_t request;
    ui_core_runtime_bridge_track_transition_ctx_t transition_ctx;
    ui_core_runtime_bridge_prepare_track_family_transition_request(&request, track, active_track_touched);
    ui_core_runtime_bridge_init_track_transition_ctx(&transition_ctx,
                                                     &request,
                                                     active_track_touched,
                                                     track,
                                                     post_sync);
    transition_ctx.family = family;
    keyboard_runtime_clear_arp_track(track);

    if (ui_core_runtime_bridge_run_track_transition_pipeline(ui_core_runtime_bridge_track_family_change_mutate,
                                                             (void *)&transition_ctx,
                                                             track) == 0U)
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
    ui_core_runtime_bridge_prepare_track_type_transition_request(&request, track, active_track_touched);
    ui_core_runtime_bridge_init_track_transition_ctx(&transition_ctx,
                                                     &request,
                                                     active_track_touched,
                                                     track,
                                                     post_sync);
    transition_ctx.type = type;
    keyboard_runtime_clear_arp_track(track);

    if (ui_core_runtime_bridge_run_track_transition_pipeline(ui_core_runtime_bridge_track_type_change_mutate,
                                                             (void *)&transition_ctx,
                                                             track) == 0U)
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
    ui_core_runtime_bridge_prepare_restore_transition_request(&request, 1U);
    ui_core_runtime_bridge_init_bulk_track_transition_ctx(&transition_ctx,
                                                          &request,
                                                          family,
                                                          type,
                                                          midi_channel,
                                                          midi_source,
                                                          post_sync);

    if (param_registry_run_track_transition_pipeline(&(const param_registry_track_transition_pipeline_cmd_t){
            .prepare_fn = NULL,
            .mutate_fn = ui_core_runtime_bridge_track_transition_mutate_bulk_restore,
            .reapply_fn = NULL,
            .seq_runtime_sync_fn = NULL,
            .ui_sync_fn = ui_core_runtime_bridge_track_transition_ui_sync_apply,
            .resume_fn = NULL,
            .ctx = (void *)&transition_ctx
        }) == 0U)
    {
        return false;
    }

    return true;
}

uint8_t ui_core_runtime_bridge_handle_routing_event(const ui_event_t *ev,
                                                    uint8_t active_track,
                                                    ui_hall_mode_t hall_mode,
                                                    uint8_t track_select_armed,
                                                    ui_core_runtime_bridge_suppress_hall_note_fn suppress_hall_note)
{
    const uint8_t is_master_fx = ui_core_runtime_bridge_track_is_master_fx(active_track);
    const uint8_t is_sampler_looper = ui_core_runtime_bridge_track_is_sampler_looper(active_track);

    if ((ev == 0)
            || ((is_master_fx == 0U) && (is_sampler_looper == 0U))
            || (hall_mode != UI_HALL_MODE_ARP)
            || (track_select_armed != 0U)
            || (ev->type != UI_EVENT_HALL_PRESS)
            || (ev->id >= UI_TRACK_COUNT))
    {
        return 0U;
    }

    const uint8_t hall = (uint8_t)ev->id;
    if (hall == active_track)
    {
        if (suppress_hall_note != 0)
        {
            suppress_hall_note(hall);
        }
        return 1U;
    }

    if (is_master_fx != 0U)
    {
        g_master_fx_route_enabled[hall] = (g_master_fx_route_enabled[hall] == 0U) ? 1U : 0U;
    }
    else if (is_sampler_looper != 0U)
    {
        g_looper_route_enabled[active_track][hall] =
            (g_looper_route_enabled[active_track][hall] == 0U) ? 1U : 0U;
    }
    if (suppress_hall_note != 0)
    {
        suppress_hall_note(hall);
    }
    return 1U;
}

uint8_t ui_core_runtime_bridge_get_master_fx_route_enabled(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 0U;
    }

    return g_master_fx_route_enabled[track];
}

uint8_t ui_core_runtime_bridge_get_looper_route_enabled(uint8_t looper_track, uint8_t source_track)
{
    if ((looper_track >= UI_TRACK_COUNT) || (source_track >= UI_TRACK_COUNT))
    {
        return 0U;
    }

    return g_looper_route_enabled[looper_track][source_track];
}

void ui_core_runtime_bridge_set_looper_route_enabled(uint8_t looper_track, uint8_t source_track, uint8_t enabled)
{
    if ((looper_track >= UI_TRACK_COUNT) || (source_track >= UI_TRACK_COUNT))
    {
        return;
    }

    g_looper_route_enabled[looper_track][source_track] = (enabled != 0U) ? 1U : 0U;
}

uint8_t ui_core_runtime_bridge_get_active_looper_record_track(uint8_t *out_track)
{
    if ((out_track == 0) || (g_active_looper_record_track >= UI_TRACK_COUNT))
    {
        return 0U;
    }

    *out_track = g_active_looper_record_track;
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

    if (ui_core_runtime_bridge_transport_play_command(ev, shift_down, track_select_armed, feedback) != 0U)
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
    if ((ev != 0)
        && (ev->type == UI_EVENT_BUTTON_PRESS)
        && (ev->id == (uint8_t)BTN_SETTINGS)
        && (shift_down != 0U)
        && (track_select_armed == 0U)
        && (mute_active == 0U)
        && (ui_core_runtime_bridge_looper_handle_save(feedback) != 0U))
    {
        return 1U;
    }

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
    ui_macro_overlay_on_hall_mode_changed();
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
