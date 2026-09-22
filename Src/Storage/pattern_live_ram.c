#include "Storage/pattern_live_ram.h"

#include <string.h>

#include "Platform/memory_layout.h"
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

typedef struct
{
    uint8_t has_snapshot;
} pattern_slot_meta_t;

typedef enum
{
    PATTERN_LOAD_IDLE = 0,
    PATTERN_LOAD_REQUESTED,
    PATTERN_LOAD_LOADING,
    PATTERN_LOAD_READY,
    PATTERN_LOAD_ERROR
} pattern_load_state_t;

typedef union
{
    persist_control_pattern_record_t pattern_record;
    persist_codec_project_workspace_t codec_workspace;
} pattern_record_lease_storage_t;

_Static_assert(sizeof(pattern_record_lease_storage_t)
                   == sizeof(persist_control_pattern_record_t),
               "Pattern record lease backing size changed");

UI_SDRAM static pattern_record_lease_storage_t g_next_record_storage;
#define g_next_record (g_next_record_storage.pattern_record)
#define g_next_pattern (g_next_record.content)
static pattern_record_lease_owner_t g_next_record_owner =
    PATTERN_RECORD_LEASE_FREE;
STORAGE_STATE_SDRAM static pattern_slot_meta_t g_pattern_slot_meta[PATTERN_BANK_COUNT][PATTERN_PER_BANK];

static persistent_pattern_default_context_t g_pattern_default_context;
static uint8_t g_pattern_default_context_valid;

static uint8_t g_active_bank;
static uint8_t g_active_pattern;
static uint8_t g_queued_valid;
static uint8_t g_queued_bank;
static uint8_t g_queued_pattern;
static uint8_t g_queued_boundary_track;
static uint32_t g_queued_boundary_generation;
static uint8_t g_pending_queue_valid;
static uint8_t g_pending_queue_bank;
static uint8_t g_pending_queue_pattern;
static uint8_t g_pending_boundary_track;
static uint32_t g_pending_boundary_generation;
static pattern_load_state_t g_pattern_load_state;
static uint8_t g_pattern_load_bank;
static uint8_t g_pattern_load_pattern;
static uint8_t g_pattern_load_last_error;
static persistence_pattern_io_workspace_t *g_pattern_io_workspace;
static pattern_control_bank_async_operation_t g_pattern_io_operation;

#define PATTERN_LOAD_ERR_INVALID_SLOT 1U
#define PATTERN_LOAD_ERR_SD_LOAD 2U
#define PATTERN_LOAD_ERR_RECORD_ACTIVE 3U
#define PATTERN_LOAD_ERR_DEFAULT_BUILD 4U

static uint8_t pattern_record_lease_owner_valid(
    pattern_record_lease_owner_t owner)
{
    return (uint8_t)(owner > PATTERN_RECORD_LEASE_FREE
        && owner <= PATTERN_RECORD_LEASE_PROJECT_SAVE);
}

persist_control_pattern_record_t *pattern_record_lease_acquire(
    pattern_record_lease_owner_t owner)
{
    if (pattern_record_lease_owner_valid(owner) == 0U
        || g_next_record_owner != PATTERN_RECORD_LEASE_FREE)
        return NULL;
    g_next_record_owner = owner;
    persist_debug_owners(owner, persistence_workspace_owner());
    return &g_next_record;
}

uint8_t pattern_record_lease_transfer(pattern_record_lease_owner_t current_owner,
                                      pattern_record_lease_owner_t next_owner)
{
    if (pattern_record_lease_owner_valid(current_owner) == 0U
        || pattern_record_lease_owner_valid(next_owner) == 0U
        || current_owner == next_owner
        || g_next_record_owner != current_owner)
        return 0U;
    g_next_record_owner = next_owner;
    persist_debug_owners(next_owner, persistence_workspace_owner());
    return 1U;
}

uint8_t pattern_record_lease_release(pattern_record_lease_owner_t owner)
{
    if (pattern_record_lease_owner_valid(owner) == 0U
        || g_next_record_owner != owner)
        return 0U;
    g_next_record_owner = PATTERN_RECORD_LEASE_FREE;
    persist_debug_owners(PATTERN_RECORD_LEASE_FREE, persistence_workspace_owner());
    return 1U;
}

pattern_record_lease_owner_t pattern_record_lease_owner(void)
{
    return g_next_record_owner;
}

persist_codec_project_workspace_t *pattern_record_lease_codec_workspace(
    pattern_record_lease_owner_t owner)
{
    if (owner != PATTERN_RECORD_LEASE_PROJECT_RESTORE
        || g_next_record_owner != owner) return NULL;
    return &g_next_record_storage.codec_workspace;
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

static uint8_t pattern_live_arm_ready_queue(uint8_t bank,
                                            uint8_t pattern,
                                            const persist_control_pattern_t *snapshot,
                                            uint8_t boundary_track,
                                            uint32_t boundary_generation)
{
    if ((snapshot == 0) || (pattern_live_slot_is_valid(bank, pattern) == 0U))
    {
        return 0U;
    }

    if (boundary_track >= SEQ_LANE_CAPACITY)
    {
        boundary_track = 0U;
    }

    if (snapshot == &g_next_pattern)
    {
        if (pattern_record_lease_transfer(PATTERN_RECORD_LEASE_PATTERN_LOAD,
                PATTERN_RECORD_LEASE_PATTERN_QUEUE_READY) == 0U)
            return 0U;
    }
    else
    {
        if (pattern_record_lease_acquire(
                PATTERN_RECORD_LEASE_PATTERN_QUEUE_READY) == NULL)
            return 0U;
        memcpy(&g_next_pattern, snapshot, sizeof(g_next_pattern));
    }
    g_queued_valid = 1U;
    g_queued_bank = bank;
    g_queued_pattern = pattern;
    g_queued_boundary_track = boundary_track;
    g_queued_boundary_generation = boundary_generation;
    if ((g_pending_queue_valid != 0U)
        && (g_pending_queue_bank == bank)
        && (g_pending_queue_pattern == pattern))
    {
        g_pending_queue_valid = 0U;
    }

    return 1U;
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

uint8_t pattern_load_request(uint8_t bank, uint8_t pattern)
{
    persist_debug_begin(PERSIST_DBG_OP_PATTERN_LOAD, bank, pattern);
    persist_debug_stage(PERSIST_DBG_STAGE_POLICY, 0);
    if (project_replacement_is_active() != 0U) return 0U;
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
    {
        if (g_next_record_owner == PATTERN_RECORD_LEASE_PATTERN_LOAD
            && g_pattern_io_operation != PATTERN_CONTROL_BANK_ASYNC_LOAD)
            (void)pattern_record_lease_release(
                PATTERN_RECORD_LEASE_PATTERN_LOAD);
        g_pattern_load_state = PATTERN_LOAD_ERROR;
        g_pattern_load_last_error = PATTERN_LOAD_ERR_INVALID_SLOT;
        persist_debug_error(PERSIST_DBG_STAGE_POLICY, PERSIST_DBG_ERROR_POLICY);
        return 0U;
    }

    if ((g_pattern_io_workspace != 0)
        && (g_pattern_io_operation == PATTERN_CONTROL_BANK_ASYNC_LOAD))
    {
        return 0U;
    }

    if (audio_recorder_is_active() != 0U)
    {
        if (g_next_record_owner == PATTERN_RECORD_LEASE_PATTERN_LOAD)
            (void)pattern_record_lease_release(
                PATTERN_RECORD_LEASE_PATTERN_LOAD);
        g_pattern_load_state = PATTERN_LOAD_ERROR;
        g_pattern_load_last_error = PATTERN_LOAD_ERR_RECORD_ACTIVE;
        return 0U;
    }

    if ((g_pattern_load_state == PATTERN_LOAD_READY)
        && (g_pattern_load_bank == bank)
        && (g_pattern_load_pattern == pattern))
    {
        return (g_next_record_owner == PATTERN_RECORD_LEASE_PATTERN_LOAD)
            ? 1U : 0U;
    }

    if (g_next_record_owner == PATTERN_RECORD_LEASE_FREE)
    {
        if (pattern_record_lease_acquire(
                PATTERN_RECORD_LEASE_PATTERN_LOAD) == NULL) return 0U;
    }
    else if (g_next_record_owner != PATTERN_RECORD_LEASE_PATTERN_LOAD)
        return 0U;

    g_pattern_load_bank = bank;
    g_pattern_load_pattern = pattern;
    g_pattern_load_last_error = 0U;
    memset(&g_next_pattern, 0, sizeof(g_next_pattern));

    const uint8_t has_snapshot = pattern_control_bank_present(bank, pattern);
    g_pattern_slot_meta[bank][pattern].has_snapshot = has_snapshot;
    if (has_snapshot == 0U)
    {
        if (pattern_live_build_default(&g_next_pattern,
                pattern_live_default_groove_seed(bank, pattern)) == 0U)
        {
            (void)pattern_record_lease_release(
                PATTERN_RECORD_LEASE_PATTERN_LOAD);
            g_pattern_load_state = PATTERN_LOAD_ERROR;
            g_pattern_load_last_error = PATTERN_LOAD_ERR_DEFAULT_BUILD;
            return 0U;
        }
        g_pattern_load_state = PATTERN_LOAD_READY;
        persist_debug_pattern_state(1U, g_queued_valid, 0U,
            ((uint32_t)g_active_bank<<16U)|g_active_pattern,
            ((uint32_t)bank<<16U)|pattern, g_queued_boundary_generation);
        persist_debug_stage(PERSIST_DBG_STAGE_READY, 0);
        return 1U;
    }

    g_pattern_load_state = PATTERN_LOAD_REQUESTED;
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
                g_pattern_slot_meta[completed_bank][completed_pattern].has_snapshot = 1U;
                if ((completed_bank == g_queued_bank)
                    && (completed_pattern == g_queued_pattern)
                    && (g_queued_valid != 0U)
                    && (g_next_record_owner
                        == PATTERN_RECORD_LEASE_PATTERN_QUEUE_READY))
                {
                    g_next_pattern = g_pattern_io_workspace->pattern;
                }
            }
            else persist_debug_error(PERSIST_DBG_STAGE_WRITE,
                                     PERSIST_DBG_ERROR_FILESYSTEM);
        }
        else if ((completed_operation == PATTERN_CONTROL_BANK_ASYNC_LOAD)
                 && (g_pattern_io_workspace != 0)
                 && (g_pattern_io_operation == PATTERN_CONTROL_BANK_ASYNC_LOAD))
        {
            if ((completed_success != 0U)
                && (g_pattern_load_state == PATTERN_LOAD_LOADING)
                && (completed_bank == g_pattern_load_bank)
                && (completed_pattern == g_pattern_load_pattern))
            {
                g_pattern_load_state = PATTERN_LOAD_READY;
                g_pattern_load_last_error = 0U;
                persist_debug_stage(PERSIST_DBG_STAGE_READY, 0);
            }
            else if (g_pattern_load_state == PATTERN_LOAD_LOADING)
            {
                g_pattern_load_state = PATTERN_LOAD_ERROR;
                g_pattern_load_last_error = PATTERN_LOAD_ERR_SD_LOAD;
                persist_debug_error(PERSIST_DBG_STAGE_READ, PERSIST_DBG_ERROR_FILESYSTEM);
            }
        }
        if (g_pattern_io_workspace != 0)
        {
            persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
            g_pattern_io_workspace = 0;
            g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;
        }
        if (completed_operation == PATTERN_CONTROL_BANK_ASYNC_LOAD
            && g_pattern_load_state != PATTERN_LOAD_READY
            && g_next_record_owner == PATTERN_RECORD_LEASE_PATTERN_LOAD)
            (void)pattern_record_lease_release(
                PATTERN_RECORD_LEASE_PATTERN_LOAD);
        return;
    }

    if ((g_pattern_load_state != PATTERN_LOAD_REQUESTED)
        && (g_pattern_load_state != PATTERN_LOAD_LOADING))
    {
        return;
    }

    if (audio_recorder_is_active() != 0U)
    {
        g_pattern_load_state = PATTERN_LOAD_ERROR;
        g_pattern_load_last_error = PATTERN_LOAD_ERR_RECORD_ACTIVE;
        if (g_pattern_io_operation != PATTERN_CONTROL_BANK_ASYNC_LOAD)
            (void)pattern_record_lease_release(
                PATTERN_RECORD_LEASE_PATTERN_LOAD);
        return;
    }

    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if (g_pattern_load_state == PATTERN_LOAD_LOADING)
    {
        return;
    }

    g_pattern_io_workspace = persistence_workspace_acquire_pattern_io();
    if (g_pattern_io_workspace == 0)
    {
        return;
    }
    if (pattern_control_bank_load_async_begin(
            g_pattern_load_bank,
            g_pattern_load_pattern,
            g_pattern_io_workspace->encoded,
            sizeof(g_pattern_io_workspace->encoded),
            &g_next_pattern) == 0U)
    {
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
        g_pattern_io_workspace = 0;
        if (pattern_control_bank_present(g_pattern_load_bank, g_pattern_load_pattern) != 0U)
        {
            return;
        }

        if (pattern_live_build_default(&g_next_pattern,
                pattern_live_default_groove_seed(g_pattern_load_bank,
                                                 g_pattern_load_pattern)) == 0U)
        {
            (void)pattern_record_lease_release(
                PATTERN_RECORD_LEASE_PATTERN_LOAD);
            g_pattern_load_state = PATTERN_LOAD_ERROR;
            g_pattern_load_last_error = PATTERN_LOAD_ERR_DEFAULT_BUILD;
            return;
        }
        g_pattern_load_state = PATTERN_LOAD_READY;
        g_pattern_load_last_error = 0U;
        return;
    }
    g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_LOAD;
    g_pattern_load_state = PATTERN_LOAD_LOADING;
}

uint8_t pattern_load_is_pending(void)
{
    return ((g_pattern_load_state == PATTERN_LOAD_REQUESTED)
            || (g_pattern_load_state == PATTERN_LOAD_LOADING)) ? 1U : 0U;
}

uint8_t pattern_load_is_ready(uint8_t *out_bank, uint8_t *out_pattern)
{
    if (g_pattern_load_state != PATTERN_LOAD_READY
        || g_next_record_owner != PATTERN_RECORD_LEASE_PATTERN_LOAD)
    {
        return 0U;
    }

    if (out_bank != 0)
    {
        *out_bank = g_pattern_load_bank;
    }
    if (out_pattern != 0)
    {
        *out_pattern = g_pattern_load_pattern;
    }
    return 1U;
}

uint8_t pattern_load_take_ready(uint8_t *out_bank, uint8_t *out_pattern, persist_control_pattern_t *out_snapshot)
{
    ++g_persist_dbg.take_ready_called;
    if ((out_snapshot == 0) || (g_pattern_load_state != PATTERN_LOAD_READY)
        || (g_next_record_owner != PATTERN_RECORD_LEASE_PATTERN_LOAD))
    {
        g_persist_dbg.take_ready_result = 0U;
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_TAKE_READY_REFUSED;
        return 0U;
    }

    if (out_bank != 0)
    {
        *out_bank = g_pattern_load_bank;
    }
    if (out_pattern != 0)
    {
        *out_pattern = g_pattern_load_pattern;
    }
    if(out_snapshot!=&g_next_pattern)
    {
        memcpy(out_snapshot, &g_next_pattern, sizeof(*out_snapshot));
        (void)pattern_record_lease_release(
            PATTERN_RECORD_LEASE_PATTERN_LOAD);
    }
    g_pattern_load_state = PATTERN_LOAD_IDLE;
    g_pattern_load_last_error = 0U;
    g_persist_dbg.take_ready_result = 1U;
    return 1U;
}

void pattern_load_cancel(void)
{
    g_pattern_load_state = PATTERN_LOAD_IDLE;
    g_pattern_load_bank = 0U;
    g_pattern_load_pattern = 0U;
    g_pattern_load_last_error = 0U;
    if (g_next_record_owner == PATTERN_RECORD_LEASE_PATTERN_LOAD
        && g_pattern_io_operation != PATTERN_CONTROL_BANK_ASYNC_LOAD)
    {
        memset(&g_next_pattern, 0, sizeof(g_next_pattern));
        (void)pattern_record_lease_release(
            PATTERN_RECORD_LEASE_PATTERN_LOAD);
    }
}

static void pattern_live_discard_ready_queue(void)
{
    if (g_next_record_owner == PATTERN_RECORD_LEASE_PATTERN_QUEUE_READY)
        (void)pattern_record_lease_release(
            PATTERN_RECORD_LEASE_PATTERN_QUEUE_READY);
    g_queued_valid = 0U;
    g_queued_bank = 0U;
    g_queued_pattern = 0U;
    g_queued_boundary_track = 0U;
    g_queued_boundary_generation = 0U;
}

void pattern_live_on_transport_stopped(void)
{
    pattern_live_cancel_recall();
}

void pattern_live_cancel_recall(void)
{
    pattern_load_cancel();
    pattern_live_discard_ready_queue();
    g_pending_queue_valid = 0U;
    g_pending_queue_bank = 0U;
    g_pending_queue_pattern = 0U;
    g_pending_boundary_track = 0U;
    g_pending_boundary_generation = 0U;
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
    persist_debug_stage(PERSIST_DBG_STAGE_QUEUE, 0); /* async job accepted */
    return 1U;
}

uint8_t pattern_live_queue_slot(uint8_t bank, uint8_t pattern, uint8_t boundary_track)
{
    persist_debug_begin(PERSIST_DBG_OP_PATTERN_QUEUE, bank, pattern);
    g_persist_dbg.active_track = boundary_track;
    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
    {
        return 0U;
    }

    if (boundary_track >= SEQ_LANE_CAPACITY)
    {
        boundary_track = 0U;
    }
    uint32_t boundary_generation = 0U;
    (void)seq_runtime_get_track_loop_generation(boundary_track, &boundary_generation);

    /* A later recall supersedes both an armed snapshot and an in-flight read.
     * The physical read may finish, but its cancelled result only releases its
     * storage; the pending coordinates below start the latest request next. */
    pattern_live_discard_ready_queue();
    if ((g_pattern_io_workspace != 0)
            && (g_pattern_io_operation == PATTERN_CONTROL_BANK_ASYNC_LOAD))
    {
        pattern_load_cancel();
        g_pending_queue_valid = 1U;
        g_pending_queue_bank = bank;
        g_pending_queue_pattern = pattern;
        g_pending_boundary_track = boundary_track;
        g_pending_boundary_generation = boundary_generation;
        return 1U;
    }
    if (pattern_load_request(bank, pattern) == 0U)
    {
        return 0U;
    }

    g_persist_dbg.transport_running = seq_runtime_is_running();
    if (g_persist_dbg.transport_running == 0U)
    {
        uint8_t ready_bank = 0U;
        uint8_t ready_pattern = 0U;
        if ((pattern_load_is_ready(&ready_bank, &ready_pattern) != 0U)
                && (ready_bank == bank) && (ready_pattern == pattern)
                && (audio_state_snapshot_control_preflight() == 0U))
        {
            g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_PREFLIGHT_BLOCKED;
            g_pending_queue_valid = 1U;
            g_pending_queue_bank = bank;
            g_pending_queue_pattern = pattern;
            g_pending_boundary_track = boundary_track;
            g_pending_boundary_generation = boundary_generation;
            return 1U;
        }
        if ((pattern_load_is_ready(&ready_bank, &ready_pattern) == 0U)
            || (ready_bank != bank)
            || (ready_pattern != pattern)
            || (pattern_load_take_ready(&ready_bank, &ready_pattern, &g_next_pattern) == 0U))
        {
            if (g_persist_dbg.take_ready_called == 0U)
                g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_NO_READY;
            g_pending_queue_valid = 1U;
            g_pending_queue_bank = bank;
            g_pending_queue_pattern = pattern;
            g_pending_boundary_track = boundary_track;
            g_pending_boundary_generation = boundary_generation;
            return 1U;
        }

        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_TRANSPORT_STOPPED_APPLY;
        persist_debug_stage(PERSIST_DBG_STAGE_APPLY,0);
        ++g_persist_dbg.apply_attempted;
        const persist_codec_result_t apply_result =
            persistent_pattern_control_apply(&g_next_pattern, 0U);
        g_persist_dbg.apply_result = (uint32_t)apply_result;
        if (apply_result != PERSIST_CODEC_OK)
        {
            g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_APPLY_FAILED;
            (void)pattern_record_lease_release(
                PATTERN_RECORD_LEASE_PATTERN_LOAD);
            g_pending_queue_valid = 0U;
            return 0U;
        }
        (void)pattern_record_lease_release(PATTERN_RECORD_LEASE_PATTERN_LOAD);
        g_active_bank = bank;
        g_active_pattern = pattern;
        g_queued_valid = 0U;
        g_pending_queue_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        undo_v2_clear_all();
        persist_debug_pattern_state(0U,0U,1U,
            ((uint32_t)bank<<16U)|pattern,0U,boundary_generation);
        persist_debug_stage(PERSIST_DBG_STAGE_SUCCESS,0);
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_APPLY_SUCCEEDED;
        return 1U;
    }

    g_pending_queue_valid = 1U;
    g_pending_queue_bank = bank;
    g_pending_queue_pattern = pattern;
    g_pending_boundary_track = boundary_track;
    g_pending_boundary_generation = boundary_generation;
    g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_TRANSPORT_RUNNING_QUEUE;

    uint8_t ready_bank = 0U;
    uint8_t ready_pattern = 0U;
    if ((pattern_load_is_ready(&ready_bank, &ready_pattern) != 0U)
        && (ready_bank == bank)
        && (ready_pattern == pattern)
        && (pattern_load_take_ready(&ready_bank, &ready_pattern, &g_next_pattern) != 0U))
    {
        persist_debug_stage(PERSIST_DBG_STAGE_QUEUE,0);
        ++g_persist_dbg.queue_attempted;
        if (pattern_live_arm_ready_queue(bank,
                pattern,&g_next_pattern,boundary_track,
                boundary_generation) == 0U)
        {
            g_persist_dbg.queue_result = 0U;
            g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_QUEUE_FAILED;
            (void)pattern_record_lease_release(
                PATTERN_RECORD_LEASE_PATTERN_LOAD);
        }
        else
        {
            g_persist_dbg.queue_result = 1U;
            g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_QUEUE_ARMED;
        }
    }
    return 1U;
}

static uint8_t pattern_live_try_take_pending_ready(void)
{
    ++g_persist_dbg.ready_consumer_calls;
    if (g_pending_queue_valid == 0U)
    {
        if (g_pattern_load_state == PATTERN_LOAD_READY)
            g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_NO_PENDING;
        return 0U;
    }

    uint8_t ready_bank = 0U;
    uint8_t ready_pattern = 0U;
    if ((g_pattern_load_state != PATTERN_LOAD_REQUESTED)
            && (g_pattern_load_state != PATTERN_LOAD_LOADING)
            && (g_pattern_load_state != PATTERN_LOAD_READY))
    {
        if (pattern_load_request(g_pending_queue_bank,
                                 g_pending_queue_pattern) == 0U)
        {
            g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_LOAD_REQUEST_REFUSED;
            return 0U;
        }
    }
    if (pattern_load_is_ready(&ready_bank, &ready_pattern) == 0U)
    {
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_NO_READY;
        return 0U;
    }

    if ((ready_bank != g_pending_queue_bank) || (ready_pattern != g_pending_queue_pattern))
    {
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_STALE_READY;
        pattern_load_cancel();
        return 0U;
    }

    g_persist_dbg.transport_running = seq_runtime_is_running();
    if ((g_persist_dbg.transport_running == 0U)
            && (audio_state_snapshot_control_preflight() == 0U))
    {
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_PREFLIGHT_BLOCKED;
        return 0U;
    }

    if (pattern_load_take_ready(&ready_bank, &ready_pattern, &g_next_pattern) == 0U)
    {
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_TAKE_READY_REFUSED;
        return 0U;
    }

    if (g_persist_dbg.transport_running == 0U)
    {
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_TRANSPORT_STOPPED_APPLY;
        persist_debug_stage(PERSIST_DBG_STAGE_APPLY,0);
        ++g_persist_dbg.apply_attempted;
        const persist_codec_result_t apply_result =
            persistent_pattern_control_apply(&g_next_pattern, 0U);
        g_persist_dbg.apply_result = (uint32_t)apply_result;
        if (apply_result != PERSIST_CODEC_OK)
        {
            g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_APPLY_FAILED;
            (void)pattern_record_lease_release(
                PATTERN_RECORD_LEASE_PATTERN_LOAD);
            g_pending_queue_valid = 0U;
            return 0U;
        }

        (void)pattern_record_lease_release(PATTERN_RECORD_LEASE_PATTERN_LOAD);

        g_active_bank = ready_bank;
        g_active_pattern = ready_pattern;
        g_queued_valid = 0U;
        g_pending_queue_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        undo_v2_clear_all();
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_APPLY_SUCCEEDED;
        return 1U;
    }

    uint32_t current_generation = 0U;
    (void)seq_runtime_get_track_loop_generation(g_pending_boundary_track, &current_generation);
    g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_TRANSPORT_RUNNING_QUEUE;
    persist_debug_stage(PERSIST_DBG_STAGE_QUEUE,0);
    ++g_persist_dbg.queue_attempted;
    const uint8_t armed = pattern_live_arm_ready_queue(ready_bank,
        ready_pattern,&g_next_pattern,g_pending_boundary_track,
        current_generation);
    if (armed == 0U)
    {
        g_persist_dbg.queue_result = 0U;
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_QUEUE_FAILED;
        (void)pattern_record_lease_release(
            PATTERN_RECORD_LEASE_PATTERN_LOAD);
    }
    else
    {
        g_persist_dbg.queue_result = 1U;
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_QUEUE_ARMED;
    }
    return armed;
}

void pattern_live_service(void)
{
    (void)pattern_live_try_take_pending_ready();

    if ((g_queued_valid == 0U)
        || (g_next_record_owner
            != PATTERN_RECORD_LEASE_PATTERN_QUEUE_READY)
        || (seq_runtime_is_running() == 0U))
    {
        return;
    }

    uint32_t current_generation = 0U;
    if (seq_runtime_get_track_loop_generation(g_queued_boundary_track, &current_generation) == 0U)
    {
        return;
    }

    uint8_t boundary_due = 0U;
    uint64_t boundary_sample = 0U;
    if (seq_runtime_get_track_next_loop_sample(
            g_queued_boundary_track, &boundary_sample) != 0U)
    {
        uint64_t now_sample = 0U;
        if (control_rt_now_sample(&now_sample) == 0U)
            return;
        boundary_due = (uint8_t)(boundary_sample
            <= control_music_output_first_unpublished_sample(now_sample));
    }
    if ((current_generation == g_queued_boundary_generation)
            && (boundary_due == 0U))
    {
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_WAIT_BOUNDARY;
        return;
    }

    persist_debug_stage(PERSIST_DBG_STAGE_APPLY,0);
    ++g_persist_dbg.apply_attempted;
    const persist_codec_result_t apply_result =
        persistent_pattern_control_apply(&g_next_pattern, 1U);
    g_persist_dbg.apply_result = (uint32_t)apply_result;
    if (apply_result == PERSIST_CODEC_OK)
    {
        g_active_bank = g_queued_bank;
        g_active_pattern = g_queued_pattern;
        g_queued_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        if ((g_pending_queue_valid != 0U)
            && (g_pending_queue_bank == g_active_bank)
            && (g_pending_queue_pattern == g_active_pattern))
        {
            g_pending_queue_valid = 0U;
        }
        undo_v2_clear_all();
        (void)pattern_record_lease_release(
            PATTERN_RECORD_LEASE_PATTERN_QUEUE_READY);
        persist_debug_pattern_state(0U,0U,1U,
            ((uint32_t)g_active_bank<<16U)|g_active_pattern,0U,current_generation);
        persist_debug_stage(PERSIST_DBG_STAGE_SUCCESS,0);
        g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_APPLY_SUCCEEDED;
    }
    else g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_APPLY_FAILED;
}

void pattern_live_init(void)
{
    g_next_record_owner = PATTERN_RECORD_LEASE_FREE;
    memset(&g_next_record, 0, sizeof(g_next_record));
    memset(&g_pattern_slot_meta, 0, sizeof(g_pattern_slot_meta));
    memset(&g_pattern_default_context, 0,
           sizeof(g_pattern_default_context));
    g_pattern_default_context.groove_seed =
        SEQ_RUNTIME_DEFAULT_GROOVE_SEED;
    g_pattern_default_context_valid = param_global_control_capture(
        &g_pattern_default_context.global_audio);
    g_active_bank = 0U;
    g_active_pattern = 0U;
    g_queued_valid = 0U;
    g_queued_bank = 0U;
    g_queued_pattern = 0U;
    g_queued_boundary_track = 0U;
    g_queued_boundary_generation = 0U;
    g_pending_queue_valid = 0U;
    g_pending_queue_bank = 0U;
    g_pending_queue_pattern = 0U;
    g_pending_boundary_track = 0U;
    g_pending_boundary_generation = 0U;
    g_pattern_load_state = PATTERN_LOAD_IDLE;
    g_pattern_load_bank = 0U;
    g_pattern_load_pattern = 0U;
    g_pattern_load_last_error = 0U;
    g_pattern_io_workspace = 0;
    g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;

    pattern_control_bank_init();

    for (uint8_t bank = 0U; bank < PATTERN_BANK_COUNT; ++bank)
    {
        for (uint8_t pattern = 0U; pattern < PATTERN_PER_BANK; ++pattern)
        {
            g_pattern_slot_meta[bank][pattern].has_snapshot = pattern_control_bank_present(bank, pattern);
        }
    }
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

uint8_t pattern_live_get_queued(uint8_t *out_valid, uint8_t *out_bank, uint8_t *out_pattern)
{
    if ((out_valid == 0) || (out_bank == 0) || (out_pattern == 0))
    {
        return 0U;
    }

    *out_valid = g_queued_valid;
    *out_bank = g_queued_bank;
    *out_pattern = g_queued_pattern;
    return 1U;
}

uint8_t pattern_live_get_queued_boundary(uint8_t *out_track,
                                         uint32_t *out_generation)
{
    if ((out_track == NULL) || (out_generation == NULL)
            || (g_queued_valid == 0U)
            || (g_queued_boundary_track >= SEQ_LANE_CAPACITY))
        return 0U;
    *out_track = g_queued_boundary_track;
    *out_generation = g_queued_boundary_generation;
    return 1U;
}

void pattern_live_set_active_state(uint8_t active_bank,
                                   uint8_t active_pattern,
                                   uint8_t queued_valid,
                                   uint8_t queued_bank,
                                   uint8_t queued_pattern,
                                   uint8_t boundary_track)
{
    if (pattern_live_slot_is_valid(active_bank, active_pattern) != 0U)
    {
        g_active_bank = active_bank;
        g_active_pattern = active_pattern;
    }

    if ((queued_valid != 0U) && (pattern_live_slot_is_valid(queued_bank, queued_pattern) != 0U))
    {
        if (boundary_track >= SEQ_LANE_CAPACITY)
        {
            boundary_track = 0U;
        }
        pattern_live_cancel_recall();
        if (pattern_load_request(queued_bank, queued_pattern) != 0U)
        {
            g_pending_queue_valid = 1U;
            g_pending_queue_bank = queued_bank;
            g_pending_queue_pattern = queued_pattern;
            g_pending_boundary_track = boundary_track;
            (void)seq_runtime_get_track_loop_generation(boundary_track, &g_pending_boundary_generation);
        }
    }
    else
    {
        pattern_live_cancel_recall();
    }
}
