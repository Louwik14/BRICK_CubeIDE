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
#include "Storage/pattern_control_bank.h"
#include "Storage/pattern_load_storage.h"
#include "Storage/persistence_workspace.h"
#include "Storage/persistent_pattern_control.h"
#include "Storage/project_load_quiesce.h"

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
static volatile uint8_t g_pattern_save_completion_valid;
static volatile uint8_t g_pattern_save_completion_bank;
static volatile uint8_t g_pattern_save_completion_pattern;
UI_SDRAM static persist_control_pattern_t g_pattern_save_candidate;
static volatile uint8_t g_pattern_save_request_valid;
static volatile uint8_t g_pattern_save_request_bank;
static volatile uint8_t g_pattern_save_request_pattern;
static persistence_pattern_io_workspace_t *g_pattern_io_workspace;
static pattern_control_bank_async_operation_t g_pattern_io_operation;

typedef enum
{
    PATTERN_LIVE_INTENT_CAPTURE = 1U,
    PATTERN_LIVE_INTENT_QUEUE = 2U
} pattern_live_intent_kind_t;

typedef struct
{
    uint8_t kind;
    uint8_t bank;
    uint8_t pattern;
    uint8_t boundary_track;
} pattern_live_intent_t;

#define PATTERN_LIVE_INTENT_CAPACITY 32U

_Static_assert((PATTERN_LIVE_INTENT_CAPACITY
                & (PATTERN_LIVE_INTENT_CAPACITY - 1U)) == 0U,
               "Pattern live intent capacity must be a power of two");
static pattern_live_intent_t g_pattern_live_intents[PATTERN_LIVE_INTENT_CAPACITY];
static volatile uint8_t g_pattern_live_intent_head;
static volatile uint8_t g_pattern_live_intent_tail;

typedef struct
{
    uint8_t active_bank;
    uint8_t active_pattern;
    uint8_t queued_valid;
    uint8_t queued_bank;
    uint8_t queued_pattern;
    uint8_t queued_boundary_track;
} pattern_live_state_request_t;

static volatile uint8_t g_pattern_live_state_request_valid;
static pattern_live_state_request_t g_pattern_live_state_request;

typedef struct
{
    uint8_t active_bank;
    uint8_t active_pattern;
    uint8_t queued_valid;
    uint8_t queued_bank;
    uint8_t queued_pattern;
    uint8_t queued_boundary_track;
    uint32_t queued_boundary_generation;
} pattern_live_public_state_t;

static volatile uint32_t g_pattern_live_public_seq;
static pattern_live_public_state_t g_pattern_live_public_state;

static void pattern_live_publish_state(void)
{
    g_pattern_live_public_seq++;
    __DMB();
    g_pattern_live_public_state = (pattern_live_public_state_t){
        g_active_bank,
        g_active_pattern,
        g_queued_valid,
        g_queued_bank,
        g_queued_pattern,
        g_queued_boundary_track,
        g_queued_boundary_generation};
    __DMB();
    g_pattern_live_public_seq++;
}

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
    if (persistent_pattern_control_apply(&g_boot_control_pattern, resume_transport) != PERSIST_CODEC_OK)
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
    pattern_storage_cancel();
    pattern_live_publish_state();
    return 1U;
}

uint8_t pattern_storage_request(uint8_t bank, uint8_t pattern)
{
    if (project_replacement_is_active() != 0U) return 0U;
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

    if ((g_pattern_io_workspace != 0)
        || (g_pattern_save_request_valid != 0U)
        || (pattern_control_bank_async_busy() != 0U))
    {
        return 0U;
    }
    g_pattern_load_bank = bank;
    g_pattern_load_pattern = pattern;
    g_pattern_load_last_error = 0U;
    g_pattern_slot_meta[bank][pattern].has_snapshot =
        pattern_control_bank_present(bank, pattern);
    g_pattern_load_state = PATTERN_LOAD_REQUESTED;
    return 1U;
}

uint8_t pattern_storage_save_busy(void)
{
    return (uint8_t)((g_pattern_save_request_valid != 0U)
        || (g_pattern_save_completion_valid != 0U)
        || (g_pattern_io_workspace != NULL)
        || (pattern_control_bank_async_busy() != 0U));
}

uint8_t pattern_storage_request_save(uint8_t bank, uint8_t pattern)
{
    if ((pattern_live_slot_is_valid(bank, pattern) == 0U)
        || (pattern_storage_save_busy() != 0U))
    {
        return 0U;
    }
    g_pattern_save_request_bank = bank;
    g_pattern_save_request_pattern = pattern;
    __DMB();
    g_pattern_save_request_valid = 1U;
    return 1U;
}

void pattern_storage_service(uint32_t byte_budget)
{
    if (byte_budget == 0U)
    {
        return;
    }

    if (g_pattern_save_completion_valid != 0U)
        return;

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
                g_pattern_save_completion_bank = completed_bank;
                g_pattern_save_completion_pattern = completed_pattern;
                __DMB();
                g_pattern_save_completion_valid = 1U;
                persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
                g_pattern_io_workspace = 0;
                g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;
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
                __DMB();
                g_pattern_load_state = PATTERN_LOAD_READY;
                g_pattern_load_last_error = 0U;
            }
            else if (g_pattern_load_state == PATTERN_LOAD_LOADING)
            {
                g_pattern_load_state = PATTERN_LOAD_ERROR;
                g_pattern_load_last_error = PATTERN_LOAD_ERR_SD_LOAD;
            }
        }
        if (g_pattern_io_workspace != 0
            && ((g_pattern_io_operation == PATTERN_CONTROL_BANK_ASYNC_SAVE)
                || (g_pattern_load_state != PATTERN_LOAD_READY)))
        {
            persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
            g_pattern_io_workspace = 0;
            g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;
        }
        return;
    }

    if ((g_pattern_save_request_valid != 0U)
        && (g_pattern_io_workspace == NULL)
        && (pattern_control_bank_async_busy() == 0U))
    {
        g_pattern_io_workspace = persistence_workspace_acquire_pattern_io();
        if (g_pattern_io_workspace == NULL)
            return;
        g_pattern_save_request_valid = 0U;
        if (pattern_control_bank_store_async_begin(
                g_pattern_save_request_bank,
                g_pattern_save_request_pattern,
                &g_pattern_save_candidate,
                g_pattern_io_workspace->encoded,
                sizeof(g_pattern_io_workspace->encoded)) == 0U)
        {
            persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
            g_pattern_io_workspace = NULL;
            return;
        }
        g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_SAVE;
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
            &g_pattern_io_workspace->pattern) == 0U)
    {
        if (pattern_control_bank_present(g_pattern_load_bank, g_pattern_load_pattern) != 0U)
        {
            persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
            g_pattern_io_workspace = 0;
            g_pattern_load_state = PATTERN_LOAD_ERROR;
            g_pattern_load_last_error = PATTERN_LOAD_ERR_SD_LOAD;
            return;
        }

        g_pattern_io_workspace->pattern = g_boot_control_pattern;
        __DMB();
        g_pattern_load_state = PATTERN_LOAD_READY;
        g_pattern_load_last_error = 0U;
        return;
    }
    g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_LOAD;
    g_pattern_load_state = PATTERN_LOAD_LOADING;
}

uint8_t pattern_storage_is_pending(void)
{
    return ((g_pattern_load_state == PATTERN_LOAD_REQUESTED)
            || (g_pattern_load_state == PATTERN_LOAD_LOADING)
            || ((g_pattern_load_state == PATTERN_LOAD_READY)
                && (g_pattern_io_workspace != NULL))) ? 1U : 0U;
}

uint8_t pattern_storage_load_available(uint8_t *out_bank, uint8_t *out_pattern)
{
    if ((g_pattern_load_state != PATTERN_LOAD_READY)
        || (g_pattern_io_workspace == NULL))
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

uint8_t pattern_storage_take_load(uint8_t *out_bank, uint8_t *out_pattern,
                                  persist_control_pattern_t *out_pattern_data)
{
    if ((out_pattern_data == 0) || (g_pattern_load_state != PATTERN_LOAD_READY)
        || (g_pattern_io_workspace == NULL))
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
    __DMB();
    *out_pattern_data = g_pattern_io_workspace->pattern;
    persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
    g_pattern_io_workspace = NULL;
    g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;
    g_pattern_load_state = PATTERN_LOAD_IDLE;
    g_pattern_load_last_error = 0U;
    return 1U;
}

void pattern_storage_cancel(void)
{
    g_pattern_load_state = PATTERN_LOAD_IDLE;
    g_pattern_load_bank = 0U;
    g_pattern_load_pattern = 0U;
    g_pattern_load_last_error = 0U;
    g_pattern_save_request_valid = 0U;
    if ((g_pattern_io_workspace != NULL)
        && (pattern_control_bank_async_busy() == 0U))
    {
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
        g_pattern_io_workspace = NULL;
        g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;
    }
    memset(&g_next_pattern, 0, sizeof(g_next_pattern));
}

static uint8_t pattern_live_capture_to_slot_control(uint8_t bank, uint8_t pattern)
{
    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
    {
        return 0U;
    }

    if ((g_pattern_save_completion_valid != 0U)
        || (pattern_storage_save_busy() != 0U))
    {
        return 0U;
    }

    if (audio_recorder_is_active() != 0U)
    {
        /* TODO pending budgeted pattern save: defer the SD store instead of blocking record drain. */
        return 0U;
    }
    if (pattern_storage_save_busy() != 0U)
        return 0U;
    if (persistent_pattern_control_capture(&g_pattern_save_candidate) != PERSIST_CODEC_OK)
        return 0U;
    return pattern_storage_request_save(bank, pattern);
}

static uint8_t pattern_live_queue_slot_control(uint8_t bank, uint8_t pattern, uint8_t boundary_track)
{
    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
    {
        return 0U;
    }

    if (pattern_storage_request(bank, pattern) == 0U)
    {
        return 0U;
    }

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
        if ((pattern_storage_load_available(&ready_bank, &ready_pattern) == 0U)
            || (ready_bank != bank)
            || (ready_pattern != pattern)
            || (pattern_storage_take_load(&ready_bank, &ready_pattern, &g_next_pattern) == 0U))
        {
            g_pending_queue_valid = 1U;
            g_pending_queue_bank = bank;
            g_pending_queue_pattern = pattern;
            g_pending_boundary_track = boundary_track;
            g_pending_boundary_generation = boundary_generation;
            undo_v2_clear_all();
            return 1U;
        }

        if (persistent_pattern_control_apply(&g_next_pattern, 0U) != PERSIST_CODEC_OK)
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
    if ((pattern_storage_load_available(&ready_bank, &ready_pattern) != 0U)
        && (ready_bank == bank)
        && (ready_pattern == pattern)
        && (pattern_storage_take_load(&ready_bank, &ready_pattern, &g_next_pattern) != 0U))
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

static uint8_t pattern_live_intent_push(pattern_live_intent_kind_t kind,
                                        uint8_t bank,
                                        uint8_t pattern,
                                        uint8_t boundary_track)
{
    const uint8_t head = g_pattern_live_intent_head;
    const uint8_t next = (uint8_t)((head + 1U) % PATTERN_LIVE_INTENT_CAPACITY);
    if (next == g_pattern_live_intent_tail)
        return 0U;
    g_pattern_live_intents[head] = (pattern_live_intent_t){
        kind, bank, pattern, boundary_track};
    __DMB();
    g_pattern_live_intent_head = next;
    return 1U;
}

static uint8_t pattern_live_intent_pop(pattern_live_intent_t *out)
{
    const uint8_t tail = g_pattern_live_intent_tail;
    if (tail == g_pattern_live_intent_head)
        return 0U;
    if (out != NULL)
        *out = g_pattern_live_intents[tail];
    __DMB();
    g_pattern_live_intent_tail =
        (uint8_t)((tail + 1U) % PATTERN_LIVE_INTENT_CAPACITY);
    return 1U;
}

uint8_t pattern_live_capture_to_slot(uint8_t bank, uint8_t pattern)
{
    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
        return 0U;
    return pattern_live_intent_push(PATTERN_LIVE_INTENT_CAPTURE, bank, pattern, 0U);
}

uint8_t pattern_live_queue_slot(uint8_t bank, uint8_t pattern, uint8_t boundary_track)
{
    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
        return 0U;
    if (boundary_track >= SEQ_LANE_CAPACITY)
        boundary_track = 0U;
    return pattern_live_intent_push(PATTERN_LIVE_INTENT_QUEUE,
                                    bank, pattern, boundary_track);
}

static void pattern_live_control_set_active_state(uint8_t active_bank,
                                                   uint8_t active_pattern,
                                                   uint8_t queued_valid,
                                                   uint8_t queued_bank,
                                                   uint8_t queued_pattern,
                                                   uint8_t boundary_track);

void pattern_live_control_process(void)
{
    pattern_live_intent_t intent;

    if (g_pattern_live_state_request_valid != 0U)
    {
        pattern_live_state_request_t request;
        __DMB();
        request = g_pattern_live_state_request;
        g_pattern_live_state_request_valid = 0U;
        pattern_live_control_set_active_state(request.active_bank,
                                               request.active_pattern,
                                               request.queued_valid,
                                               request.queued_bank,
                                               request.queued_pattern,
                                               request.queued_boundary_track);
    }

    if (g_pattern_save_completion_valid != 0U)
    {
        const uint8_t bank = g_pattern_save_completion_bank;
        const uint8_t pattern = g_pattern_save_completion_pattern;
        __DMB();
        g_pattern_save_completion_valid = 0U;
        if ((g_queued_valid != 0U)
            && (g_queued_bank == bank)
            && (g_queued_pattern == pattern))
            g_next_pattern = g_pattern_save_candidate;
    }

    while (pattern_live_intent_pop(&intent) != 0U)
    {
        if (intent.kind == PATTERN_LIVE_INTENT_CAPTURE)
            (void)pattern_live_capture_to_slot_control(intent.bank, intent.pattern);
        else if (intent.kind == PATTERN_LIVE_INTENT_QUEUE)
            (void)pattern_live_queue_slot_control(intent.bank,
                                                   intent.pattern,
                                                   intent.boundary_track);
    }
    pattern_live_publish_state();
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
    if (pattern_storage_load_available(&ready_bank, &ready_pattern) == 0U)
    {
        return 0U;
    }

    if ((ready_bank != g_pending_queue_bank) || (ready_pattern != g_pending_queue_pattern))
    {
        return 0U;
    }

    if (pattern_storage_take_load(&ready_bank, &ready_pattern, &g_next_pattern) == 0U)
    {
        return 0U;
    }

    if (seq_runtime_is_running() == 0U)
    {
        if (persistent_pattern_control_apply(&g_next_pattern, 0U) != PERSIST_CODEC_OK)
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
        goto publish;
    }

    uint32_t current_generation = 0U;
    if (seq_runtime_get_track_loop_generation(g_queued_boundary_track, &current_generation) == 0U)
    {
        goto publish;
    }

    uint8_t boundary_due = 0U;
    uint64_t boundary_sample = 0U;
    if (seq_runtime_get_track_next_loop_sample(
            g_queued_boundary_track, &boundary_sample) != 0U)
    {
        uint64_t now_sample = 0U;
        if (control_rt_now_sample(&now_sample) == 0U)
            goto publish;
        boundary_due = (uint8_t)(boundary_sample
            <= control_music_output_first_unpublished_sample(now_sample));
    }
    if ((current_generation == g_queued_boundary_generation)
            && (boundary_due == 0U))
    {
        goto publish;
    }

    if (persistent_pattern_control_apply(&g_next_pattern, 1U) == PERSIST_CODEC_OK)
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

publish:
    pattern_live_publish_state();
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
    g_pattern_save_completion_valid = 0U;
    g_pattern_save_completion_bank = 0U;
    g_pattern_save_completion_pattern = 0U;
    g_pattern_save_request_valid = 0U;
    g_pattern_save_request_bank = 0U;
    g_pattern_save_request_pattern = 0U;
    memset(&g_pattern_save_candidate, 0, sizeof(g_pattern_save_candidate));
    g_pattern_live_intent_head = 0U;
    g_pattern_live_intent_tail = 0U;
    g_pattern_live_state_request_valid = 0U;
    memset(&g_pattern_live_state_request, 0, sizeof(g_pattern_live_state_request));
    g_pattern_live_public_seq = 0U;
    memset(&g_pattern_live_public_state, 0, sizeof(g_pattern_live_public_state));
    g_pattern_io_workspace = 0;
    g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;

    if (persistent_pattern_control_capture(&g_boot_control_pattern) == PERSIST_CODEC_OK)
    {
        g_next_pattern=g_boot_control_pattern;
    }
    else
    {
        (void)persistent_pattern_control_capture(&g_boot_control_pattern);
    }
}

void pattern_live_storage_init(void)
{
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

    pattern_live_public_state_t state;
    uint32_t first;
    uint32_t second;
    do
    {
        first = g_pattern_live_public_seq;
        if ((first & 1U) != 0U) { second = first; continue; }
        __DMB();
        state = g_pattern_live_public_state;
        __DMB();
        second = g_pattern_live_public_seq;
    } while ((first != second) || ((second & 1U) != 0U));
    *out_bank = state.active_bank;
    *out_pattern = state.active_pattern;
    return 1U;
}

uint8_t pattern_live_get_queued(uint8_t *out_valid, uint8_t *out_bank, uint8_t *out_pattern)
{
    if ((out_valid == 0) || (out_bank == 0) || (out_pattern == 0))
    {
        return 0U;
    }

    pattern_live_public_state_t state;
    uint32_t first;
    uint32_t second;
    do
    {
        first = g_pattern_live_public_seq;
        if ((first & 1U) != 0U) { second = first; continue; }
        __DMB();
        state = g_pattern_live_public_state;
        __DMB();
        second = g_pattern_live_public_seq;
    } while ((first != second) || ((second & 1U) != 0U));
    *out_valid = state.queued_valid;
    *out_bank = state.queued_bank;
    *out_pattern = state.queued_pattern;
    return 1U;
}

uint8_t pattern_live_get_queued_boundary(uint8_t *out_track,
                                         uint32_t *out_generation)
{
    if ((out_track == NULL) || (out_generation == NULL))
        return 0U;
    pattern_live_public_state_t state;
    uint32_t first;
    uint32_t second;
    do
    {
        first = g_pattern_live_public_seq;
        if ((first & 1U) != 0U) { second = first; continue; }
        __DMB();
        state = g_pattern_live_public_state;
        __DMB();
        second = g_pattern_live_public_seq;
    } while ((first != second) || ((second & 1U) != 0U));
    if ((state.queued_valid == 0U)
        || (state.queued_boundary_track >= SEQ_LANE_CAPACITY))
        return 0U;
    *out_track = state.queued_boundary_track;
    *out_generation = state.queued_boundary_generation;
    return 1U;
}

static void pattern_live_control_set_active_state(uint8_t active_bank,
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
        g_queued_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        if (pattern_storage_request(queued_bank, queued_pattern) != 0U)
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

void pattern_live_set_active_state(uint8_t active_bank,
                                   uint8_t active_pattern,
                                   uint8_t queued_valid,
                                   uint8_t queued_bank,
                                   uint8_t queued_pattern,
                                   uint8_t boundary_track)
{
    g_pattern_live_state_request = (pattern_live_state_request_t){
        active_bank, active_pattern, queued_valid, queued_bank,
        queued_pattern, boundary_track};
    __DMB();
    g_pattern_live_state_request_valid = 1U;
}
