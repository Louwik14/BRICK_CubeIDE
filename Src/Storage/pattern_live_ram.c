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
#include "Storage/project_product.h"
#include "Storage/sd_access_gate.h"
#include "Storage/storage_io_wakeup.h"
#include "App/control_rt_wakeup.h"
#include "App/control_domain.h"
#include "UI/ui_service_wakeup.h"
#include "stm32h7xx.h"

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
static volatile uint8_t g_pattern_save_completion_success;
static volatile uint8_t g_pattern_save_completion_bank;
static volatile uint8_t g_pattern_save_completion_pattern;
static volatile uint8_t g_pattern_save_request_valid;
static volatile uint8_t g_pattern_save_request_bank;
static volatile uint8_t g_pattern_save_request_pattern;
static persistence_pattern_io_workspace_t *g_pattern_io_workspace;
static pattern_control_bank_async_operation_t g_pattern_io_operation;
static volatile uint8_t g_pattern_operation_engaged;
static uint8_t g_pattern_operation;
static uint8_t g_pattern_operation_bank;
static uint8_t g_pattern_operation_pattern;
static uint8_t g_pattern_operation_boundary_track;
static uint32_t g_pattern_operation_media_epoch;
static pattern_live_terminal_t g_pattern_terminal;
static volatile uint8_t g_pattern_terminal_valid;

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
#define PATTERN_LOAD_ERR_APPLY 4U
#define PATTERN_SAVE_ERR_CAPTURE 5U
#define PATTERN_SAVE_ERR_IO 6U

static uint8_t pattern_live_slot_is_valid(uint8_t bank, uint8_t pattern)
{
    return (bank < PATTERN_BANK_COUNT) && (pattern < PATTERN_PER_BANK);
}

static void pattern_live_release_operation(void)
{
    if (g_pattern_io_workspace != NULL)
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PATTERN_IO);
    g_pattern_io_workspace = NULL;
    g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;
    g_pattern_save_request_valid = 0U;
    g_pattern_operation_engaged = 0U;
}

static void pattern_live_publish_terminal(uint8_t success, uint8_t diagnostic)
{
    const uint8_t operation = g_pattern_operation;
    const uint8_t bank = g_pattern_operation_bank;
    const uint8_t pattern = g_pattern_operation_pattern;
    pattern_live_release_operation();
    g_pattern_terminal = (pattern_live_terminal_t){
        operation, bank, pattern, (success != 0U) ? 1U : 0U, diagnostic};
    __DMB();
    g_pattern_terminal_valid = 1U;
    ui_service_wakeup(UI_SERVICE_WAKE_INPUT);
}

uint8_t pattern_live_terminal_available(void)
{
    return g_pattern_terminal_valid;
}

uint8_t pattern_live_operation_busy(void)
{
    return g_pattern_operation_engaged;
}

uint8_t pattern_live_take_terminal(pattern_live_terminal_t *out_terminal)
{
    if ((out_terminal == NULL) || (g_pattern_terminal_valid == 0U)) return 0U;
    *out_terminal = g_pattern_terminal;
    __DMB();
    g_pattern_terminal_valid = 0U;
    return 1U;
}

static void pattern_storage_publish_save_completion(uint8_t bank,
                                                    uint8_t pattern,
                                                    uint8_t success)
{
    g_pattern_save_completion_bank = bank;
    g_pattern_save_completion_pattern = pattern;
    g_pattern_save_completion_success = (success != 0U) ? 1U : 0U;
    __DMB();
    g_pattern_save_completion_valid = 1U;
    control_rt_wakeup(CONTROL_RT_WAKE_STORAGE);
}

static void pattern_storage_wake_owner(void)
{
    storage_io_owner_wakeup(STORAGE_OWNER_PATTERN);
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
    if ((g_pattern_operation_engaged == 0U)
        || (g_pattern_operation != PATTERN_LIVE_OPERATION_RECALL)
        || (g_pattern_operation_bank != bank)
        || (g_pattern_operation_pattern != pattern)
        || (g_pattern_io_workspace == NULL)) return 0U;

    g_pattern_load_bank = bank;
    g_pattern_load_pattern = pattern;
    g_pattern_load_last_error = 0U;
    g_pattern_slot_meta[bank][pattern].has_snapshot =
        pattern_control_bank_present(bank, pattern);
    g_pattern_load_state = PATTERN_LOAD_REQUESTED;
    if((sd_preview_is_active() != 0U)
            || (sd_preview_get_state() == SD_PREVIEW_STATE_STOPPING))
        sd_preview_request_stop();
    storage_io_owner_set(STORAGE_OWNER_PATTERN);
    storage_io_wakeup(STORAGE_IO_WAKE_RUNNABLE);
    return 1U;
}

uint8_t pattern_storage_save_busy(void)
{
    return (uint8_t)(((g_pattern_operation_engaged != 0U)
                      && (g_pattern_operation == PATTERN_LIVE_OPERATION_STORE))
        || (g_pattern_save_request_valid != 0U)
        || (g_pattern_save_completion_valid != 0U)
        || (g_pattern_io_workspace != NULL)
        || (pattern_control_bank_async_busy() != 0U));
}

uint8_t pattern_storage_request_save(uint8_t bank, uint8_t pattern)
{
    if ((g_pattern_operation_engaged == 0U)
        || (g_pattern_operation != PATTERN_LIVE_OPERATION_STORE)
        || (g_pattern_operation_bank != bank)
        || (g_pattern_operation_pattern != pattern)
        || (g_pattern_io_workspace == NULL)) return 0U;
    g_pattern_save_request_bank = bank;
    g_pattern_save_request_pattern = pattern;
    __DMB();
    g_pattern_save_request_valid = 1U;
    storage_io_owner_set(STORAGE_OWNER_PATTERN);
    storage_io_wakeup(STORAGE_IO_WAKE_RUNNABLE);
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
                g_pattern_slot_meta[completed_bank][completed_pattern].has_snapshot = 1U;
            g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;
            pattern_storage_publish_save_completion(completed_bank,
                                                    completed_pattern,
                                                    completed_success);
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
                control_rt_wakeup(CONTROL_RT_WAKE_STORAGE);
            }
            else if (g_pattern_load_state == PATTERN_LOAD_LOADING)
            {
                g_pattern_load_state = PATTERN_LOAD_ERROR;
                g_pattern_load_last_error = PATTERN_LOAD_ERR_SD_LOAD;
                control_rt_wakeup(CONTROL_RT_WAKE_STORAGE);
            }
        }
        return;
    }

    if ((g_pattern_save_request_valid != 0U)
        && (g_pattern_io_workspace != NULL)
        && (pattern_control_bank_async_busy() == 0U))
    {
        g_pattern_save_request_valid = 0U;
        if (pattern_control_bank_store_async_begin(
                g_pattern_save_request_bank,
                g_pattern_save_request_pattern,
                &g_pattern_io_workspace->pattern,
                g_pattern_io_workspace->encoded,
                sizeof(g_pattern_io_workspace->encoded)) == 0U)
        {
            pattern_storage_publish_save_completion(
                g_pattern_save_request_bank,
                g_pattern_save_request_pattern,
                0U);
            return;
        }
        g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_SAVE;
        pattern_storage_wake_owner();
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
        control_rt_wakeup(CONTROL_RT_WAKE_STORAGE);
        return;
    }

    if ((sd_access_storage_status() != SD_STORAGE_STATUS_READY)
        || (sd_access_media_epoch() != g_pattern_operation_media_epoch))
    {
        g_pattern_load_state = PATTERN_LOAD_ERROR;
        g_pattern_load_last_error = PATTERN_LOAD_ERR_SD_LOAD;
        control_rt_wakeup(CONTROL_RT_WAKE_STORAGE);
        return;
    }

    if ((sd_preview_is_active() != 0U)
            || (sd_preview_get_state() == SD_PREVIEW_STATE_STOPPING))
    {
        if (sd_preview_stop() == 0U)
        {
            storage_io_owner_wait_resource(STORAGE_OWNER_PATTERN);
            return;
        }
    }

    if (g_pattern_load_state == PATTERN_LOAD_LOADING)
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
        if (g_pattern_slot_meta[g_pattern_load_bank][g_pattern_load_pattern].has_snapshot != 0U)
        {
            g_pattern_load_state = PATTERN_LOAD_ERROR;
            g_pattern_load_last_error = PATTERN_LOAD_ERR_SD_LOAD;
            control_rt_wakeup(CONTROL_RT_WAKE_STORAGE);
            return;
        }

        g_pattern_io_workspace->pattern = g_boot_control_pattern;
        __DMB();
        g_pattern_load_state = PATTERN_LOAD_READY;
        g_pattern_load_last_error = 0U;
        control_rt_wakeup(CONTROL_RT_WAKE_STORAGE);
        return;
    }
    g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_LOAD;
    g_pattern_load_state = PATTERN_LOAD_LOADING;
    pattern_storage_wake_owner();
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
    g_pattern_io_operation = PATTERN_CONTROL_BANK_ASYNC_NONE;
    return 1U;
}

void pattern_storage_cancel(void)
{
    g_pattern_load_state = PATTERN_LOAD_IDLE;
    g_pattern_load_bank = 0U;
    g_pattern_load_pattern = 0U;
    g_pattern_load_last_error = 0U;
    g_pattern_save_request_valid = 0U;
    g_pattern_save_completion_success = 0U;
    if (pattern_control_bank_async_busy() == 0U)
        pattern_live_release_operation();
    memset(&g_next_pattern, 0, sizeof(g_next_pattern));
}

static uint8_t pattern_live_capture_to_slot_control(uint8_t bank, uint8_t pattern)
{
    if ((g_pattern_io_workspace == NULL)
        || (persistent_pattern_control_capture(&g_pattern_io_workspace->pattern)
            != PERSIST_CODEC_OK)) return 0U;
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
    g_pending_queue_valid = 1U;
    g_pending_queue_bank = bank;
    g_pending_queue_pattern = pattern;
    g_pending_boundary_track = boundary_track;
    g_pending_boundary_generation = boundary_generation;

    return 1U;
}

uint8_t pattern_live_capture_to_slot(uint8_t bank, uint8_t pattern)
{
    if ((pattern_live_slot_is_valid(bank, pattern) == 0U)
        || (sd_access_storage_status() != SD_STORAGE_STATUS_READY)
        || (audio_recorder_is_active() != 0U)) return 0U;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if ((g_pattern_operation_engaged != 0U)
        || (g_pattern_terminal_valid != 0U)
        || (project_product_ui_busy() != 0U)
        || (control_domain_settings_asset_mutation_active() != 0U)
        || (sd_access_storage_status() != SD_STORAGE_STATUS_READY)
        || (audio_recorder_is_active() != 0U))
    {
        __set_PRIMASK(primask);
        return 0U;
    }
    g_pattern_io_workspace = persistence_workspace_acquire_pattern_io();
    if (g_pattern_io_workspace == NULL)
    {
        __set_PRIMASK(primask);
        return 0U;
    }
    g_pattern_operation = PATTERN_LIVE_OPERATION_STORE;
    g_pattern_operation_bank = bank;
    g_pattern_operation_pattern = pattern;
    g_pattern_operation_boundary_track = 0U;
    g_pattern_operation_media_epoch = sd_access_media_epoch();
    g_pattern_operation_engaged = 1U;
    const uint8_t accepted = control_domain_request_pattern(
        &(control_pattern_intent_t){CONTROL_PATTERN_STORE, bank, pattern, 0U});
    if (accepted == 0U) pattern_live_release_operation();
    __set_PRIMASK(primask);
    return accepted;
}

uint8_t pattern_live_queue_slot(uint8_t bank, uint8_t pattern, uint8_t boundary_track)
{
    if (pattern_live_slot_is_valid(bank, pattern) == 0U)
        return 0U;
    if (boundary_track >= SEQ_LANE_CAPACITY)
        boundary_track = 0U;
    if ((sd_access_storage_status() != SD_STORAGE_STATUS_READY)
        || (audio_recorder_client_is_active(AUDIO_RECORDER_CLIENT_LOOPER) != 0U))
        return 0U;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if ((g_pattern_operation_engaged != 0U)
        || (g_pattern_terminal_valid != 0U)
        || (project_product_ui_busy() != 0U)
        || (control_domain_settings_asset_mutation_active() != 0U)
        || (sd_access_storage_status() != SD_STORAGE_STATUS_READY)
        || (audio_recorder_client_is_active(AUDIO_RECORDER_CLIENT_LOOPER) != 0U))
    {
        __set_PRIMASK(primask);
        return 0U;
    }
    g_pattern_io_workspace = persistence_workspace_acquire_pattern_io();
    if (g_pattern_io_workspace == NULL)
    {
        __set_PRIMASK(primask);
        return 0U;
    }
    g_pattern_operation = PATTERN_LIVE_OPERATION_RECALL;
    g_pattern_operation_bank = bank;
    g_pattern_operation_pattern = pattern;
    g_pattern_operation_boundary_track = boundary_track;
    g_pattern_operation_media_epoch = sd_access_media_epoch();
    g_pattern_operation_engaged = 1U;
    const uint8_t accepted = control_domain_request_pattern(
        &(control_pattern_intent_t){CONTROL_PATTERN_RECALL, bank, pattern,
                                    boundary_track});
    if (accepted == 0U) pattern_live_release_operation();
    __set_PRIMASK(primask);
    return accepted;
}

void pattern_live_control_process_intent(uint8_t operation, uint8_t bank,
                                         uint8_t pattern, uint8_t boundary_track)
{
    if ((g_pattern_operation_engaged == 0U)
        || (g_pattern_operation != operation)
        || (g_pattern_operation_bank != bank)
        || (g_pattern_operation_pattern != pattern)) return;
    if (operation == PATTERN_LIVE_OPERATION_STORE)
    {
        if (pattern_live_capture_to_slot_control(bank, pattern) == 0U)
            pattern_live_publish_terminal(0U, PATTERN_SAVE_ERR_CAPTURE);
    }
    else if (operation == PATTERN_LIVE_OPERATION_RECALL)
    {
        if (pattern_live_queue_slot_control(bank, pattern, boundary_track) == 0U)
            pattern_live_publish_terminal(0U, PATTERN_LOAD_ERR_SD_LOAD);
    }
}

static void pattern_live_control_set_active_state(uint8_t active_bank,
                                                   uint8_t active_pattern,
                                                   uint8_t queued_valid,
                                                   uint8_t queued_bank,
                                                   uint8_t queued_pattern,
                                                   uint8_t boundary_track);

void pattern_live_control_process(void)
{
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
        const uint8_t success = g_pattern_save_completion_success;
        __DMB();
        g_pattern_save_completion_valid = 0U;
        (void)bank;
        (void)pattern;
        pattern_live_publish_terminal(success,
            (success != 0U) ? 0U : PATTERN_SAVE_ERR_IO);
    }
    if ((g_pattern_operation_engaged != 0U)
        && (g_pattern_operation == PATTERN_LIVE_OPERATION_RECALL)
        && (g_pattern_load_state == PATTERN_LOAD_ERROR))
    {
        g_pending_queue_valid = 0U;
        g_queued_valid = 0U;
        pattern_live_publish_terminal(0U, g_pattern_load_last_error);
        g_pattern_load_state = PATTERN_LOAD_IDLE;
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
            g_pattern_load_state = PATTERN_LOAD_IDLE;
            g_pending_queue_valid = 0U;
            pattern_live_publish_terminal(0U, PATTERN_LOAD_ERR_APPLY);
            return 0U;
        }

        g_active_bank = ready_bank;
        g_active_pattern = ready_pattern;
        g_queued_valid = 0U;
        g_pending_queue_valid = 0U;
        g_queued_boundary_track = 0U;
        g_queued_boundary_generation = 0U;
        undo_v2_clear_all();
        g_pattern_load_state = PATTERN_LOAD_IDLE;
        pattern_live_publish_terminal(1U, 0U);
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

    if (g_queued_valid == 0U)
    {
        goto publish;
    }

    if (seq_runtime_is_running() == 0U)
        goto apply;

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

apply:
    if (persistent_pattern_control_apply(&g_next_pattern,
            (seq_runtime_is_running() != 0U) ? 1U : 0U) == PERSIST_CODEC_OK)
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
        g_pattern_load_state = PATTERN_LOAD_IDLE;
        pattern_live_publish_terminal(1U, 0U);
    }
    else
    {
        g_queued_valid = 0U;
        g_pending_queue_valid = 0U;
        g_pattern_load_state = PATTERN_LOAD_IDLE;
        pattern_live_publish_terminal(0U, PATTERN_LOAD_ERR_APPLY);
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
    g_pattern_save_completion_success = 0U;
    g_pattern_save_completion_bank = 0U;
    g_pattern_save_completion_pattern = 0U;
    g_pattern_save_request_valid = 0U;
    g_pattern_save_request_bank = 0U;
    g_pattern_save_request_pattern = 0U;
    g_pattern_operation_engaged = 0U;
    g_pattern_operation = PATTERN_LIVE_OPERATION_STORE;
    g_pattern_operation_bank = 0U;
    g_pattern_operation_pattern = 0U;
    g_pattern_operation_boundary_track = 0U;
    g_pattern_operation_media_epoch = 0U;
    memset(&g_pattern_terminal, 0, sizeof(g_pattern_terminal));
    g_pattern_terminal_valid = 0U;
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
    control_rt_wakeup(CONTROL_RT_WAKE_STORAGE);
}
