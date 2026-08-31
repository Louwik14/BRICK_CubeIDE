#include "Storage/pattern_live_ram.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Storage/audio_recorder.h"
#include "Storage/sd_preview.h"
#include "Storage/undo_v2.h"
#include "UI/ui_core.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Track/control_music_output.h"
#include "IPC/live_clock_control.h"
#include "Storage/pattern_control_bank.h"
#include "Storage/persistence_workspace.h"
#include "Storage/persistent_pattern_control.h"

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

UI_SDRAM static persist_control_pattern_record_t g_next_record;
#define g_next_pattern (g_next_record.content)
UI_SDRAM static persist_control_pattern_t g_boot_control_pattern;
STORAGE_STATE_SDRAM static pattern_slot_meta_t g_pattern_slot_meta[PATTERN_BANK_COUNT][PATTERN_PER_BANK];

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

static uint8_t pattern_live_slot_is_valid(uint8_t bank, uint8_t pattern)
{
    return (bank < PATTERN_BANK_COUNT) && (pattern < PATTERN_PER_BANK);
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

    if (snapshot != &g_next_pattern)
    {
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


uint8_t pattern_live_apply_boot_snapshot(uint8_t resume_transport)
{
    if (persistent_pattern_control_install_restored(&g_boot_control_pattern, resume_transport) != PERSIST_CODEC_OK)
    {
        return 0U;
    }

    if(persistent_pattern_control_capture(&g_next_pattern)!=PERSIST_CODEC_OK)return 0U;
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
    pattern_load_cancel();
    return 1U;
}

uint8_t pattern_load_request(uint8_t bank, uint8_t pattern)
{
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
    {
        g_pattern_load_state = PATTERN_LOAD_ERROR;
        g_pattern_load_last_error = PATTERN_LOAD_ERR_INVALID_SLOT;
        return 0U;
    }

    if ((g_pattern_io_workspace != 0)
        && (g_pattern_io_operation == PATTERN_CONTROL_BANK_ASYNC_LOAD))
    {
        return 0U;
    }

    if (audio_recorder_client_is_active(AUDIO_RECORDER_CLIENT_LOOPER) != 0U)
    {
        g_pattern_load_state = PATTERN_LOAD_ERROR;
        g_pattern_load_last_error = PATTERN_LOAD_ERR_RECORD_ACTIVE;
        return 0U;
    }

    if ((g_pattern_load_state == PATTERN_LOAD_READY)
        && (g_pattern_load_bank == bank)
        && (g_pattern_load_pattern == pattern))
    {
        return 1U;
    }

    g_pattern_load_bank = bank;
    g_pattern_load_pattern = pattern;
    g_pattern_load_last_error = 0U;
    memset(&g_next_pattern, 0, sizeof(g_next_pattern));

    const uint8_t has_snapshot = pattern_control_bank_present(bank, pattern);
    g_pattern_slot_meta[bank][pattern].has_snapshot = has_snapshot;
    if (has_snapshot == 0U)
    {
        g_next_pattern=g_boot_control_pattern;
        g_pattern_load_state = PATTERN_LOAD_READY;
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
                g_pattern_slot_meta[completed_bank][completed_pattern].has_snapshot = 1U;
                if ((completed_bank == g_queued_bank)
                    && (completed_pattern == g_queued_pattern)
                    && (g_queued_valid != 0U))
                {
                    g_next_pattern = g_pattern_io_workspace->pattern;
                }
            }
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
            }
            else if (g_pattern_load_state == PATTERN_LOAD_LOADING)
            {
                g_pattern_load_state = PATTERN_LOAD_ERROR;
                g_pattern_load_last_error = PATTERN_LOAD_ERR_SD_LOAD;
            }
        }
        if (g_pattern_io_workspace != 0)
        {
            persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
            g_pattern_io_workspace = 0;
            g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;
        }
        return;
    }

    if ((g_pattern_load_state != PATTERN_LOAD_REQUESTED)
        && (g_pattern_load_state != PATTERN_LOAD_LOADING))
    {
        return;
    }

    if (audio_recorder_client_is_active(AUDIO_RECORDER_CLIENT_LOOPER) != 0U)
    {
        g_pattern_load_state = PATTERN_LOAD_ERROR;
        g_pattern_load_last_error = PATTERN_LOAD_ERR_RECORD_ACTIVE;
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

        g_next_pattern=g_boot_control_pattern;
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
    if (g_pattern_load_state != PATTERN_LOAD_READY)
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
    if ((out_snapshot == 0) || (g_pattern_load_state != PATTERN_LOAD_READY))
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
    if(out_snapshot!=&g_next_pattern)memcpy(out_snapshot, &g_next_pattern, sizeof(*out_snapshot));
    g_pattern_load_state = PATTERN_LOAD_IDLE;
    g_pattern_load_last_error = 0U;
    return 1U;
}

void pattern_load_cancel(void)
{
    g_pattern_load_state = PATTERN_LOAD_IDLE;
    g_pattern_load_bank = 0U;
    g_pattern_load_pattern = 0U;
    g_pattern_load_last_error = 0U;
    memset(&g_next_pattern, 0, sizeof(g_next_pattern));
}

uint8_t pattern_live_capture_to_slot(uint8_t bank, uint8_t pattern)
{
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
    return 1U;
}

uint8_t pattern_live_queue_slot(uint8_t bank, uint8_t pattern)
{
    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
    {
        return 0U;
    }

    if (pattern_load_request(bank, pattern) == 0U)
    {
        return 0U;
    }

    uint8_t boundary_track = ui_get_active_track();
    if (boundary_track >= SEQ_LANE_CAPACITY)
    {
        boundary_track = 0U;
    }
    uint32_t boundary_generation = 0U;
    (void)seq_runtime_get_track_loop_generation(boundary_track, &boundary_generation);

    if (seq_runtime_is_running() == 0U)
    {
        uint8_t ready_bank = 0U;
        uint8_t ready_pattern = 0U;
        if ((pattern_load_is_ready(&ready_bank, &ready_pattern) == 0U)
            || (ready_bank != bank)
            || (ready_pattern != pattern)
            || (pattern_load_take_ready(&ready_bank, &ready_pattern, &g_next_pattern) == 0U))
        {
            g_pending_queue_valid = 1U;
            g_pending_queue_bank = bank;
            g_pending_queue_pattern = pattern;
            g_pending_boundary_track = boundary_track;
            g_pending_boundary_generation = boundary_generation;
            undo_v2_clear_all();
            return 1U;
        }

        if (persistent_pattern_control_install_restored(&g_next_pattern, 0U) != PERSIST_CODEC_OK)
        {
            return 0U;
        }
        g_active_bank = bank;
        g_active_pattern = pattern;
        g_queued_valid = 0U;
        g_pending_queue_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        undo_v2_clear_all();
        return 1U;
    }

    g_pending_queue_valid = 1U;
    g_pending_queue_bank = bank;
    g_pending_queue_pattern = pattern;
    g_pending_boundary_track = boundary_track;
    g_pending_boundary_generation = boundary_generation;

    uint8_t ready_bank = 0U;
    uint8_t ready_pattern = 0U;
    if ((pattern_load_is_ready(&ready_bank, &ready_pattern) != 0U)
        && (ready_bank == bank)
        && (ready_pattern == pattern)
        && (pattern_load_take_ready(&ready_bank, &ready_pattern, &g_next_pattern) != 0U))
    {
        (void)pattern_live_arm_ready_queue(bank,
                                           pattern,
                                           &g_next_pattern,
                                           boundary_track,
                                           boundary_generation);
    }
    undo_v2_clear_all();
    return 1U;
}

uint8_t pattern_live_get_control_boot(persist_control_pattern_t*out){if(out==NULL)return 0U;*out=g_boot_control_pattern;return 1U;}

static uint8_t pattern_live_try_take_pending_ready(void)
{
    if (g_pending_queue_valid == 0U)
    {
        return 0U;
    }

    uint8_t ready_bank = 0U;
    uint8_t ready_pattern = 0U;
    if (pattern_load_is_ready(&ready_bank, &ready_pattern) == 0U)
    {
        return 0U;
    }

    if ((ready_bank != g_pending_queue_bank) || (ready_pattern != g_pending_queue_pattern))
    {
        return 0U;
    }

    if (pattern_load_take_ready(&ready_bank, &ready_pattern, &g_next_pattern) == 0U)
    {
        return 0U;
    }

    if (seq_runtime_is_running() == 0U)
    {
        if (persistent_pattern_control_install_restored(&g_next_pattern, 0U) != PERSIST_CODEC_OK)
        {
            return 0U;
        }

        g_active_bank = ready_bank;
        g_active_pattern = ready_pattern;
        g_queued_valid = 0U;
        g_pending_queue_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        undo_v2_clear_all();
        return 1U;
    }

    uint32_t current_generation = 0U;
    (void)seq_runtime_get_track_loop_generation(g_pending_boundary_track, &current_generation);
    return pattern_live_arm_ready_queue(ready_bank,
                                        ready_pattern,
                                        &g_next_pattern,
                                        g_pending_boundary_track,
                                        current_generation);
}

void pattern_live_service(void)
{
    (void)pattern_live_try_take_pending_ready();

    if ((g_queued_valid == 0U) || (seq_runtime_is_running() == 0U))
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
        (void)live_clock_read_audio_sample(&now_sample);
        boundary_due = (uint8_t)(boundary_sample
            <= control_music_output_first_unpublished_sample(now_sample));
    }
    if ((current_generation == g_queued_boundary_generation)
            && (boundary_due == 0U))
    {
        return;
    }

    if (persistent_pattern_control_install_restored(&g_next_pattern, 1U) == PERSIST_CODEC_OK)
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
    }
}

void pattern_live_init(void)
{
    memset(&g_next_record, 0, sizeof(g_next_record));
    memset(&g_boot_control_pattern,0,sizeof(g_boot_control_pattern));
    memset(&g_pattern_slot_meta, 0, sizeof(g_pattern_slot_meta));
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

    if (persistent_pattern_control_capture(&g_boot_control_pattern) == PERSIST_CODEC_OK)
    {
        g_next_pattern=g_boot_control_pattern;
        pattern_control_bank_init();
    }
    else
    {
        (void)persistent_pattern_control_capture(&g_boot_control_pattern);
        pattern_control_bank_init();
    }

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
                                   uint8_t queued_pattern)
{
    if (pattern_live_slot_is_valid(active_bank, active_pattern) != 0U)
    {
        g_active_bank = active_bank;
        g_active_pattern = active_pattern;
    }

    if ((queued_valid != 0U) && (pattern_live_slot_is_valid(queued_bank, queued_pattern) != 0U))
    {
        uint8_t boundary_track = ui_get_active_track();
        if (boundary_track >= SEQ_LANE_CAPACITY)
        {
            boundary_track = 0U;
        }
        g_queued_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
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
        g_queued_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        g_pending_queue_valid = 0U;
    }
}
