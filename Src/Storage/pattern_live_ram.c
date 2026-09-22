#include "Storage/pattern_live_ram.h"

#include <string.h>

#include "Storage/audio_recorder.h"
#include "Storage/sd_preview.h"
#include "Storage/undo_v2.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Track/control_music_output.h"
#include "ControlRT/control_rt_publication.h"
#include "ControlRT/audio_state_snapshot_control.h"
#include "Storage/pattern_control_bank.h"
#include "Storage/persistence_workspace.h"
#include "Storage/persistent_pattern_control.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/persistence_debug.h"

#define PATTERN_BANK_COUNT 16U
#define PATTERN_PER_BANK   16U

typedef enum
{
    PATTERN_CANDIDATE_EMPTY = 0,
    PATTERN_CANDIDATE_REQUESTED,
    PATTERN_CANDIDATE_LOADING,
    PATTERN_CANDIDATE_PENDING
} pattern_candidate_phase_t;

typedef struct
{
    pattern_candidate_phase_t phase;
    uint32_t request_generation;
    uint32_t boundary_generation;
    uint8_t bank;
    uint8_t pattern;
    uint8_t boundary_track;
    uint8_t boundary_armed;
} pattern_candidate_t;

static persistent_pattern_default_context_t g_pattern_default_context;
static uint8_t g_pattern_default_context_valid;

static uint8_t g_active_bank;
static uint8_t g_active_pattern;
static pattern_candidate_t g_pattern_candidate;
static uint32_t g_pattern_request_generation;
static uint32_t g_pattern_io_request_generation;
static persistence_pattern_io_workspace_t *g_pattern_io_workspace;
static pattern_control_bank_async_operation_t g_pattern_io_operation;

static void pattern_debug_state(void)
{
    const uint32_t candidate =
        (g_pattern_candidate.phase == PATTERN_CANDIDATE_EMPTY) ? 0U
        : ((uint32_t)g_pattern_candidate.bank << 16U)
            | g_pattern_candidate.pattern;
    persist_debug_pattern_state((uint32_t)g_pattern_candidate.phase,
        ((uint32_t)g_active_bank << 16U) | g_active_pattern,
        candidate, g_pattern_candidate.request_generation,
        (g_pattern_io_operation == PATTERN_CONTROL_BANK_ASYNC_LOAD)
            ? g_pattern_io_request_generation : 0U,
        g_pattern_candidate.boundary_track,
        g_pattern_candidate.boundary_armed,
        g_pattern_candidate.boundary_generation);
}

static uint8_t pattern_live_slot_is_valid(uint8_t bank, uint8_t pattern)
{
    return (bank < PATTERN_BANK_COUNT) && (pattern < PATTERN_PER_BANK);
}

static uint32_t pattern_live_default_groove_seed(uint8_t bank, uint8_t pattern)
{
    uint32_t value = UINT32_C(0x42524943)
        ^ ((uint32_t)(bank + 1U) * UINT32_C(0x9E3779B9))
        ^ ((uint32_t)(pattern + 1U) * UINT32_C(0x85EBCA6B));
    value ^= value >> 16U;
    value *= UINT32_C(0x7FEB352D);
    value ^= value >> 15U;
    value *= UINT32_C(0x846CA68B);
    value ^= value >> 16U;
    return (value != 0U) ? value : UINT32_C(1);
}

uint8_t pattern_live_build_default(persist_control_pattern_t *out,
                                   uint32_t groove_seed)
{
    if ((out == NULL) || (g_pattern_default_context_valid == 0U)) return 0U;
    persistent_pattern_default_context_t context = g_pattern_default_context;
    context.groove_seed = groove_seed;
    return (persistent_pattern_control_build_defaults(out, &context)
            == PERSIST_CODEC_OK) ? 1U : 0U;
}

static uint32_t pattern_candidate_next_generation(void)
{
    ++g_pattern_request_generation;
    if (g_pattern_request_generation == 0U) ++g_pattern_request_generation;
    return g_pattern_request_generation;
}

static void pattern_candidate_clear(void)
{
    memset(&g_pattern_candidate, 0, sizeof(g_pattern_candidate));
    pattern_debug_state();
}

static void pattern_candidate_release_payload(void)
{
    if ((g_pattern_io_workspace != NULL)
        && (g_pattern_io_operation == PATTERN_CONTROL_BANK_ASYNC_NONE))
    {
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
        g_pattern_io_workspace = NULL;
    }
}

static uint8_t pattern_candidate_apply(uint8_t resume_transport)
{
    if ((g_pattern_candidate.phase != PATTERN_CANDIDATE_PENDING)
        || (g_pattern_io_workspace == NULL)
        || (g_pattern_io_operation != PATTERN_CONTROL_BANK_ASYNC_NONE))
        return 0U;

    persist_debug_stage(PERSIST_DBG_STAGE_APPLY, 0);
    ++g_persist_dbg.apply_attempted;
    const persist_codec_result_t result =
        persistent_pattern_control_apply(&g_pattern_io_workspace->pattern,
                                         resume_transport);
    g_persist_dbg.apply_result = (uint32_t)result;
    if (result != PERSIST_CODEC_OK)
    {
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_APPLY_FAILED;
        persist_debug_error(PERSIST_DBG_STAGE_APPLY,(int32_t)result);
        pattern_candidate_clear();
        pattern_candidate_release_payload();
        return 0U;
    }

    g_active_bank = g_pattern_candidate.bank;
    g_active_pattern = g_pattern_candidate.pattern;
    const uint32_t boundary_generation =
        g_pattern_candidate.boundary_generation;
    const uint32_t request_generation =
        g_pattern_candidate.request_generation;
    pattern_candidate_clear();
    undo_v2_clear_all();
    pattern_candidate_release_payload();
    persist_debug_publication(1U, 1U);
    persistent_pattern_control_sync_ui_after_commit();
    pattern_debug_state();
    g_persist_dbg.request_generation = request_generation;
    g_persist_dbg.boundary_generation = boundary_generation;
    persist_debug_stage(PERSIST_DBG_STAGE_SUCCESS, 0);
    g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_APPLY_SUCCEEDED;
    return 1U;
}

static void pattern_candidate_decoded(void)
{
    if (g_pattern_candidate.phase != PATTERN_CANDIDATE_LOADING
        && g_pattern_candidate.phase != PATTERN_CANDIDATE_REQUESTED)
        return;
    const persist_codec_result_t validation =
        persistent_pattern_control_validate(&g_pattern_io_workspace->pattern);
    if (validation != PERSIST_CODEC_OK)
    {
        persist_debug_error(PERSIST_DBG_STAGE_VALIDATE,(int32_t)validation);
        pattern_candidate_clear();
        pattern_candidate_release_payload();
        return;
    }

    g_pattern_candidate.phase = PATTERN_CANDIDATE_PENDING;
    g_pattern_candidate.boundary_armed = seq_runtime_is_running();
    g_pattern_candidate.boundary_generation = 0U;
    if (g_pattern_candidate.boundary_armed != 0U)
    {
        (void)seq_runtime_get_track_loop_generation(
            g_pattern_candidate.boundary_track,
            &g_pattern_candidate.boundary_generation);
        persist_debug_stage(PERSIST_DBG_STAGE_CANDIDATE, 0);
        g_persist_dbg.decision_reason =
            PERSIST_DBG_DECISION_TRANSPORT_RUNNING_PENDING;
        pattern_debug_state();
        return;
    }

    persist_debug_stage(PERSIST_DBG_STAGE_CANDIDATE, 0);
    pattern_debug_state();
    g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_TRANSPORT_STOPPED_APPLY;
    if (audio_state_snapshot_control_preflight() != 0U)
        (void)pattern_candidate_apply(0U);
    else
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_PREFLIGHT_BLOCKED;
}

static uint8_t pattern_candidate_request(uint8_t bank, uint8_t pattern,
                                         uint8_t boundary_track)
{
    persist_debug_begin(PERSIST_DBG_OP_PATTERN_LOAD, bank, pattern);
    persist_debug_stage(PERSIST_DBG_STAGE_POLICY, 0);
    if ((project_replacement_is_active() != 0U)
        || (pattern_live_slot_is_valid(bank, pattern) == 0U)
        || (audio_recorder_is_active() != 0U))
    {
        persist_debug_error(PERSIST_DBG_STAGE_POLICY, PERSIST_DBG_ERROR_POLICY);
        return 0U;
    }
    if (sd_preview_is_active() != 0U) sd_preview_stop();
    if (boundary_track >= SEQ_LANE_CAPACITY) boundary_track = 0U;

    const uint8_t load_in_flight = (uint8_t)((g_pattern_io_workspace != 0)
        && (g_pattern_io_operation == PATTERN_CONTROL_BANK_ASYNC_LOAD));
    if (load_in_flight == 0U) pattern_candidate_release_payload();

    pattern_candidate_clear();
    g_pattern_candidate.phase = PATTERN_CANDIDATE_REQUESTED;
    g_pattern_candidate.request_generation = pattern_candidate_next_generation();
    g_pattern_candidate.bank = bank;
    g_pattern_candidate.pattern = pattern;
    g_pattern_candidate.boundary_track = boundary_track;
    pattern_debug_state();
    return 1U;
}

void pattern_load_service(uint32_t byte_budget)
{
    if (byte_budget == 0U)
    {
        return;
    }

    pattern_control_bank_async_service();
    pattern_control_bank_async_operation_t completed_operation;
    uint8_t completed_bank = 0U;
    uint8_t completed_pattern = 0U;
    uint8_t completed_success = 0U;
    uint8_t completed_candidate_ready = 0U;
    uint8_t completed_candidate_failed = 0U;
    if (pattern_control_bank_async_take_result(&completed_operation,
                                               &completed_bank,
                                               &completed_pattern,
                                               &completed_success) != 0U)
    {
        if ((completed_operation == PATTERN_CONTROL_BANK_ASYNC_SAVE)
            && (g_pattern_io_workspace != 0)
            && (g_pattern_io_operation == PATTERN_CONTROL_BANK_ASYNC_SAVE))
        {
            if (completed_success != 0U)
            {
                persist_debug_stage(PERSIST_DBG_STAGE_SUCCESS,0);
            }
            else persist_debug_error(PERSIST_DBG_STAGE_WRITE,
                                     PERSIST_DBG_ERROR_FILESYSTEM);
        }
        else if ((completed_operation == PATTERN_CONTROL_BANK_ASYNC_LOAD)
                 && (g_pattern_io_workspace != 0)
                 && (g_pattern_io_operation == PATTERN_CONTROL_BANK_ASYNC_LOAD))
        {
            const uint8_t matches = (uint8_t)(
                (g_pattern_candidate.phase == PATTERN_CANDIDATE_LOADING)
                && (g_pattern_io_request_generation
                    == g_pattern_candidate.request_generation)
                && (completed_bank == g_pattern_candidate.bank)
                && (completed_pattern == g_pattern_candidate.pattern));
            if ((completed_success != 0U) && (matches != 0U))
            {
                completed_candidate_ready = 1U;
            }
            else if (matches != 0U)
            {
                completed_candidate_failed = 1U;
            }
        }
        if (g_pattern_io_workspace != 0)
        {
            g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;
            if (completed_candidate_ready == 0U)
            {
                persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
                g_pattern_io_workspace = 0;
            }
        }
        if (completed_candidate_failed != 0U)
        {
            pattern_candidate_clear();
            persist_debug_error(PERSIST_DBG_STAGE_READ,
                                PERSIST_DBG_ERROR_FILESYSTEM);
        }
        else if (completed_candidate_ready != 0U)
        {
            pattern_candidate_decoded();
        }
        return;
    }

    if (g_pattern_candidate.phase != PATTERN_CANDIDATE_REQUESTED) return;
    if ((g_pattern_io_workspace != 0)
        || (pattern_control_bank_async_busy() != 0U)) return;
    if (audio_recorder_is_active() != 0U)
    {
        pattern_live_cancel_recall();
        return;
    }
    if (sd_preview_is_active() != 0U) sd_preview_stop();

    if (g_pattern_io_workspace == NULL)
    {
        g_pattern_io_workspace = persistence_workspace_acquire_pattern_io();
        if (g_pattern_io_workspace == NULL) return;
        g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;
    }
    else if (g_pattern_io_operation != PATTERN_CONTROL_BANK_ASYNC_NONE) return;

    if (pattern_control_bank_present(g_pattern_candidate.bank,
                                     g_pattern_candidate.pattern) == 0U)
    {
        if (pattern_live_build_default(&g_pattern_io_workspace->pattern,
                pattern_live_default_groove_seed(g_pattern_candidate.bank,
                                                 g_pattern_candidate.pattern)) == 0U)
        {
            pattern_live_cancel_recall();
            return;
        }
        pattern_candidate_decoded();
        return;
    }

    if (pattern_control_bank_load_async_begin(
            g_pattern_candidate.bank,
            g_pattern_candidate.pattern,
            g_pattern_io_workspace->encoded,
            sizeof(g_pattern_io_workspace->encoded),
            &g_pattern_io_workspace->pattern) == 0U)
    {
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
        g_pattern_io_workspace = 0;
        return;
    }
    g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_LOAD;
    g_pattern_io_request_generation = g_pattern_candidate.request_generation;
    g_pattern_candidate.phase = PATTERN_CANDIDATE_LOADING;
    pattern_debug_state();
}

uint8_t pattern_load_is_pending(void)
{
    return ((g_pattern_candidate.phase == PATTERN_CANDIDATE_REQUESTED)
            || (g_pattern_candidate.phase == PATTERN_CANDIDATE_LOADING))
        ? 1U : 0U;
}

void pattern_live_on_transport_stopped(void)
{
    pattern_live_cancel_recall();
}

void pattern_live_cancel_recall(void)
{
    (void)pattern_candidate_next_generation();
    pattern_candidate_clear();
    pattern_candidate_release_payload();
}

uint8_t pattern_live_capture_to_slot(uint8_t bank, uint8_t pattern)
{
    persist_debug_begin(PERSIST_DBG_OP_PATTERN_SAVE, bank, pattern);
    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
    {
        return 0U;
    }

    if ((g_pattern_io_workspace != 0)
        || (pattern_control_bank_async_busy() != 0U))
    {
        return 0U;
    }

    g_pattern_io_workspace = persistence_workspace_acquire_pattern_io();
    if (g_pattern_io_workspace == 0) return 0U;
    persist_control_pattern_t *const captured = &g_pattern_io_workspace->pattern;

    if (persistent_pattern_control_capture(captured) != PERSIST_CODEC_OK)
    {
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
        g_pattern_io_workspace = 0;
        return 0U;
    }

    if (audio_recorder_is_active() != 0U)
    {
        /* TODO pending budgeted pattern save: defer the SD store instead of blocking record drain. */
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
        g_pattern_io_workspace = 0;
        return 0U;
    }

    if (pattern_control_bank_store_async_begin(
            bank,
            pattern,
            captured,
            g_pattern_io_workspace->encoded,
            sizeof(g_pattern_io_workspace->encoded)) == 0U)
    {
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
        g_pattern_io_workspace = 0;
        return 0U;
    }
    g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_SAVE;
    persist_debug_stage(PERSIST_DBG_STAGE_ASYNC, 0);
    return 1U;
}

uint8_t pattern_live_request_slot(uint8_t bank, uint8_t pattern, uint8_t boundary_track)
{
    const uint8_t accepted = pattern_candidate_request(
        bank, pattern, boundary_track);
    if (accepted != 0U) pattern_load_service(1U);
    return accepted;
}

void pattern_live_service(void)
{
    if ((g_pattern_candidate.phase != PATTERN_CANDIDATE_PENDING)
        || (g_pattern_io_workspace == NULL)
        || (g_pattern_io_operation != PATTERN_CONTROL_BANK_ASYNC_NONE)) return;

    g_persist_dbg.transport_running = seq_runtime_is_running();
    if (g_pattern_candidate.boundary_armed == 0U)
    {
        if (g_persist_dbg.transport_running != 0U)
        {
            g_pattern_candidate.boundary_armed = 1U;
            (void)seq_runtime_get_track_loop_generation(
                g_pattern_candidate.boundary_track,
                &g_pattern_candidate.boundary_generation);
            pattern_debug_state();
            return;
        }
        if (audio_state_snapshot_control_preflight() == 0U)
        {
            g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_PREFLIGHT_BLOCKED;
            return;
        }
        (void)pattern_candidate_apply(0U);
        return;
    }
    if (g_persist_dbg.transport_running == 0U)
    {
        pattern_live_cancel_recall();
        return;
    }

    uint32_t current_generation = 0U;
    if (seq_runtime_get_track_loop_generation(
            g_pattern_candidate.boundary_track, &current_generation) == 0U) return;
    g_persist_dbg.boundary_observed_generation = current_generation;

    uint8_t boundary_due = 0U;
    uint64_t boundary_sample = 0U;
    if (seq_runtime_get_track_next_loop_sample(
            g_pattern_candidate.boundary_track, &boundary_sample) != 0U)
    {
        uint64_t now_sample = 0U;
        if (control_rt_now_sample(&now_sample) == 0U)
            return;
        boundary_due = (uint8_t)(boundary_sample
            <= control_music_output_first_unpublished_sample(now_sample));
    }
    g_persist_dbg.boundary_due = boundary_due;
    if ((current_generation == g_pattern_candidate.boundary_generation)
            && (boundary_due == 0U))
    {
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_WAIT_BOUNDARY;
        return;
    }

    (void)pattern_candidate_apply(1U);
}

void pattern_live_init(void)
{
    memset(&g_pattern_default_context, 0,
           sizeof(g_pattern_default_context));
    g_pattern_default_context.groove_seed =
        SEQ_RUNTIME_DEFAULT_GROOVE_SEED;
    g_pattern_default_context_valid = param_global_control_capture(
        &g_pattern_default_context.global_audio);
    g_active_bank = 0U;
    g_active_pattern = 0U;
    memset(&g_pattern_candidate, 0, sizeof(g_pattern_candidate));
    g_pattern_request_generation = 0U;
    g_pattern_io_request_generation = 0U;
    g_pattern_io_workspace = 0;
    g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;

    pattern_control_bank_init();
    pattern_debug_state();
}

uint8_t pattern_live_get_active(uint8_t *out_bank, uint8_t *out_pattern)
{
    if ((out_bank == 0) || (out_pattern == 0))
    {
        return 0U;
    }

    *out_bank = g_active_bank;
    *out_pattern = g_active_pattern;
    return 1U;
}

uint8_t pattern_live_get_pending(uint8_t *out_valid, uint8_t *out_bank, uint8_t *out_pattern)
{
    if ((out_valid == 0) || (out_bank == 0) || (out_pattern == 0))
    {
        return 0U;
    }

    *out_valid = (uint8_t)((g_pattern_candidate.phase
            == PATTERN_CANDIDATE_PENDING)
        && (g_pattern_candidate.boundary_armed != 0U));
    *out_bank = g_pattern_candidate.bank;
    *out_pattern = g_pattern_candidate.pattern;
    return 1U;
}

void pattern_live_publish_active(uint8_t active_bank, uint8_t active_pattern)
{
    if (pattern_live_slot_is_valid(active_bank, active_pattern) != 0U)
    {
        g_active_bank = active_bank;
        g_active_pattern = active_pattern;
    }

    pattern_live_cancel_recall();
    pattern_debug_state();
}
