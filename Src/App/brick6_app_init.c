/**
 * @file brick6_app_init.c
 */

#include "App/brick6_app_init.h"

#include "App/power_shutdown.h"
#include "App/control_domain.h"
#include "App/encoder_control_dispatcher.h"
#include "IPC/live_clock_control.h"
#include "App/control_clipboard.h"
#include "App/control_rt_sampled_state.h"
#include "App/control_rt_wakeup.h"
#include "Audio/audio_domain.h"
#include "Audio/audio.h"
#include "Board/board_usb.h"
#include "MIDI/midi.h"
#include "MIDI/midi_host.h"
#include "sdram.h"

#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_import.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_stream_transport.h"
#include "Sampler/sample_stream_admission.h"
#include "App/brick6_master_control.h"
#include "Storage/boot_context_flash.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/pattern_load_storage.h"
#include "Storage/patch_product.h"
#include "Storage/project_control.h"
#include "Storage/project_product.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/storage_catalog.h"
#include "Storage/sd_preview.h"
#include "Storage/sd_access_gate.h"
#include "Storage/audio_recorder.h"
#include "Storage/sample_capture.h"
#include "Storage/waveform_cache.h"
#include "Storage/wav_convert.h"
#include "Storage/settings_storage_service.h"
#include "Storage/storage_io_wakeup.h"
#include "SD/sd_scheduler_runtime.h"
#include "SD/sd_block_device.h"
#include "Platform/brick6_sd_config.h"

#include "App/Hall/hall_keyboard_bridge.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_loop.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_clock_bridge.h"
#include "UI/ui_active_track_sync.h"
#include "UI/ui_boot_loading.h"
#include "UI/ui_core.h"
#include "UI/ui_event.h"
#include "UI/ui_renderer_oled.h"
#include "UI/ui_tasklet.h"
#include "UI/ui_service_wakeup.h"
#include "UI/ui_hall_mode_flow.h"
#include "UI/pages/ui_page_settings.h"

#define BRICK6_STREAM_SERVICE_BYTE_BUDGET      (32768U)
#define BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES  (8192U)
#define BRICK6_STREAM_OTHER_SD_QUANTUM_FRAMES (1024U)

typedef enum
{
    BRICK6_BOOT_WAIT_MASTER = 0,
    BRICK6_BOOT_AUDIO_RUNNING,
    BRICK6_BOOT_AUDIO_FAILED
} brick6_boot_audio_state_t;

static brick6_boot_audio_state_t g_boot_audio_state;
static uint8_t g_control_calibration_saved;
static uint8_t g_control_calibration_seen;
static uint8_t g_control_calibration_stage;
static uint8_t g_control_calibration_counts[HALL_KEY_COUNT];
static uint8_t g_control_user_calibration_handled;
static uint8_t g_control_user_calibration_seen;
static uint8_t g_control_user_calibration_stage;
static uint8_t g_control_user_calibration_count;

static void brick6_app_control_calibration_service(uint32_t now_ms)
{
    uint8_t calibration_changed = 0U;
    if (hall_calibration_is_active() != 0U)
    {
        hall_calibration_process();
        if ((g_control_calibration_seen == 0U)
            || (g_control_calibration_stage != hall_calibration_get_stage()))
        {
            calibration_changed = 1U;
        }
        for (uint8_t key = 0U; key < HALL_KEY_COUNT; ++key)
        {
            const uint8_t count = hall_calibration_get_count(key);
            if ((g_control_calibration_seen == 0U)
                || (g_control_calibration_counts[key] != count))
            {
                calibration_changed = 1U;
            }
            g_control_calibration_counts[key] = count;
        }
        g_control_calibration_stage = hall_calibration_get_stage();
        g_control_calibration_seen = 1U;
    }
    else
    {
        g_control_calibration_seen = 0U;
    }

    if (hall_calibration_is_done() != 0U)
    {
        if (g_control_calibration_saved == 0U)
        {
            hall_calibration_save();
            g_control_calibration_saved = 1U;
            calibration_changed = 1U;
        }
    }
    else
    {
        g_control_calibration_saved = 0U;
    }

    if (hall_user_calibration_is_active() != 0U)
    {
        if (g_control_user_calibration_seen == 0U)
            g_control_user_calibration_handled = 0U;
        hall_user_calibration_process();
        if ((g_control_user_calibration_seen == 0U)
            || (g_control_user_calibration_stage != hall_user_calibration_get_stage())
            || (g_control_user_calibration_count != hall_user_calibration_get_stage_count()))
            calibration_changed = 1U;
        g_control_user_calibration_stage = (uint8_t)hall_user_calibration_get_stage();
        g_control_user_calibration_count = hall_user_calibration_get_stage_count();
        g_control_user_calibration_seen = 1U;
    }
    else
    {
        g_control_user_calibration_seen = 0U;
    }
    if (hall_user_calibration_is_done() != 0U)
    {
        if (g_control_user_calibration_handled == 0U)
        {
            g_control_user_calibration_handled = 1U;
            if (hall_user_calibration_was_successful() != 0U)
            {
                hall_set_velocity_profile((uint8_t)HALL_VEL_PROFILE_USER);
                hall_calibration_save();
            }
            calibration_changed = 1U;
        }
        else if (hall_user_calibration_was_successful() == 0U)
        {
            /* Retry timing is owned by the calibration state machine. */
            uint32_t retry_deadline_ms = 0U;
            if (hall_user_calibration_next_deadline(now_ms, &retry_deadline_ms) != 0U
                && ((int32_t)(now_ms - retry_deadline_ms) >= 0))
            {
                hall_user_calibration_start();
                g_control_user_calibration_handled = 0U;
                calibration_changed = 1U;
            }
        }
    }
    else
    {
        g_control_user_calibration_handled = 0U;
    }

    if (calibration_changed != 0U)
        ui_service_dirty_set();
}

/* ============================================================
   INIT APP
   ============================================================ */

/**
 * @brief Point d'entrée brick6_app_init.
 *
 * Rôle:
 * - Initialisation globale de l'application.
 */
void brick6_app_init(void)
{
    SDRAM_Init();
    storage_io_init();
    boot_context_flash_init();

    static const brick6_audio_boot_intent_t audio_boot = {
        .sample_rate_hz = 48000.0f,
        .postgain = 1.0f,
        .output_compensation = 1.0f,
        .fx_slot_count = BRICK6_AUDIO_BOOT_FX_SLOT_COUNT,
        .fx_slots = {
            { .slot = 0U, .type = (uint8_t)BRICK6_AUDIO_BOOT_FX_EQ3 },
            { .slot = 2U, .type = (uint8_t)BRICK6_AUDIO_BOOT_FX_COMP_LAB },
        },
    };
    control_domain_init();
    audio_domain_init(&audio_boot);
    live_clock_control_init();
    control_domain_start(audio_boot.postgain, audio_boot.output_compensation);
    g_boot_audio_state = (audio_start() != 0U)
        ? BRICK6_BOOT_WAIT_MASTER
        : BRICK6_BOOT_AUDIO_FAILED;
}

static uint8_t g_storage_initialized;

static void brick6_app_storage_init(void)
{
    if (g_storage_initialized != 0U)
    {
        return;
    }

    (void)waveform_cache_ensure_dirs();
    wav_loader_catalog_init_load();
    pattern_live_storage_init();
    patch_product_storage_init();
    project_product_storage_init();
    g_storage_initialized = 1U;
}


void brick6_app_storage_dispatch_once(void)
{
    brick6_app_storage_init();
    const uint32_t runnable = storage_io_owner_snapshot();

    if ((runnable & (1UL << STORAGE_OWNER_STREAM)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_STREAM);
        sample_global_pool_storage_request_service();
        sd_scheduler_runtime_service();
        sample_stream_transport_worker_poll();
        sd_access_gate_set_streaming_critical(
            sample_stream_manager_has_pending_sd_work());
        if (!((sample_stream_manager_io_in_flight() == 0U)
              && (multi_sample_load_is_active() != 0U)))
        {
            sample_cache_service(BRICK6_STREAM_SERVICE_BYTE_BUDGET);
            sd_access_gate_set_streaming_critical(
                sample_stream_manager_has_pending_sd_work());
        }
        storage_settings_service_owner(STORAGE_OWNER_STREAM);
    }
    if ((runnable & (1UL << STORAGE_OWNER_RECORDER)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_RECORDER);
        audio_recorder_service();
        sample_capture_recorder_storage_service();
    }
    if ((runnable & (1UL << STORAGE_OWNER_PROJECT)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_PROJECT);
        project_load_quiesce_storage_retire();
        project_product_storage_request_service();
        project_product_save_service();
        project_product_load_service();
    }
    if ((runnable & (1UL << STORAGE_OWNER_PATTERN)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_PATTERN);
        pattern_storage_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES / 2U);
    }
    if ((runnable & (1UL << STORAGE_OWNER_PATCH)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_PATCH);
        patch_product_apply_service();
        patch_product_storage_request_service();
    }
    if ((runnable & (1UL << STORAGE_OWNER_SAMPLE_RAM)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_SAMPLE_RAM);
        sampler_ram_pool_storage_request_service();
        sampler_ram_pool_service_retire();
        sampler_ram_pool_load_async_service();
        sampler_ram_pool_waveform_service(BRICK6_STREAM_OTHER_SD_QUANTUM_FRAMES);
        storage_settings_service_owner(STORAGE_OWNER_SAMPLE_RAM);
    }
    if ((runnable & (1UL << STORAGE_OWNER_WAVETABLE)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_WAVETABLE);
        wavetable_pool_storage_request_service();
        wavetable_pool_service_retire();
        wavetable_pool_load_async_service();
        storage_settings_service_owner(STORAGE_OWNER_WAVETABLE);
    }
    if ((runnable & (1UL << STORAGE_OWNER_MULTI)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_MULTI);
        multi_sample_pool_storage_request_service();
        multi_sample_import_storage_request_service();
        multi_sample_import_storage_delete_service();
        multi_sample_load_storage_request_service();
        sd_scheduler_runtime_service();
        sample_stream_transport_worker_poll();
        multi_sample_pool_service_retire();
        multi_sample_service_load(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
        sd_scheduler_runtime_service();
        sample_stream_transport_worker_poll();
        storage_settings_service_owner(STORAGE_OWNER_MULTI);
    }
    if ((runnable & (1UL << STORAGE_OWNER_CATALOG)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_CATALOG);
        storage_catalog_service();
        wav_loader_catalog_storage_service();
        storage_settings_service_owner(STORAGE_OWNER_CATALOG);
    }
    if ((runnable & (1UL << STORAGE_OWNER_WAV_CONVERT)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_WAV_CONVERT);
        wav_convert_service(65536U);
        storage_settings_service_owner(STORAGE_OWNER_WAV_CONVERT);
    }
    if ((runnable & (1UL << STORAGE_OWNER_WAVEFORM_CACHE)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_WAVEFORM_CACHE);
        sample_capture_storage_service();
        waveform_cache_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
    }
    if ((runnable & (1UL << STORAGE_OWNER_PREVIEW)) != 0U)
    {
        storage_io_owner_clear(STORAGE_OWNER_PREVIEW);
        sd_preview_process();
    }
}

static void brick6_app_control_process_storage_completions(void)
{
    uint8_t project_slot = 0U;
    uint8_t project_success = 0U;
    (void)project_product_save_take_result(&project_slot, &project_success);

    multi_sample_load_completion_t multi_completion;
    if (multi_sample_load_take_completion(&multi_completion) != 0U)
    {
        if (multi_completion.logical_id != MULTI_SAMPLE_POOL_INVALID_ID)
        {
            (void)project_control_complete_multi_runtime(
                multi_completion.logical_id, multi_completion.path,
                multi_completion.instrument_id, multi_completion.success);
        }
        else if (multi_completion.success != 0U)
        {
            uint16_t logical = UINT16_MAX;
            (void)project_control_register_multi_runtime(
                multi_completion.path, multi_completion.instrument_id, &logical);
        }
    }
    {
        sampler_ram_result_t result;
        uint16_t ram_slot = SAMPLER_RAM_POOL_INVALID_SLOT;
        uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const char *path = 0;
        const uint32_t request_id = sampler_ram_pool_load_async_request_id();
        if ((sampler_ram_pool_load_async_requester() == SAMPLER_RAM_REQUESTER_UI)
            && sampler_ram_pool_load_async_take_result(
                request_id, &result, &ram_slot, &global_slot, &path) != 0U
            && result == SAMPLER_RAM_RESULT_OK && path != 0)
        {
            (void)project_control_register_sample_runtime(
                PERSIST_ASSET_SAMPLE_RAM, path, global_slot, 0);
        }
    }
    {
        wavetable_result_t result;
        uint16_t wavetable_slot = WAVETABLE_POOL_INVALID_SLOT;
        uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const char *path = 0;
        const uint32_t request_id = wavetable_pool_load_async_request_id();
        if ((wavetable_pool_load_async_requester() == WAVETABLE_REQUESTER_UI)
            && wavetable_pool_load_async_take_result(
                request_id, &result, &wavetable_slot, &global_slot, &path) != 0U
            && result == WAVETABLE_RESULT_OK && path != 0)
        {
            (void)project_control_register_wavetable_runtime(
                path, global_slot, 0);
        }
    }
}

void brick6_app_control_process_causes(uint32_t wake_flags)
{
    const uint32_t now_ms = HAL_GetTick();
    control_rt_sampled_state_process(now_ms);
    if (power_shutdown_is_active() != 0U)
    {
        seq_runtime_control_deadline_disarm();
        return;
    }

    sample_capture_control_service();
    brick6_app_control_calibration_service(now_ms);

    if (ui_boot_loading_restore_pending() != 0U
        && sd_access_storage_status() == SD_STORAGE_STATUS_READY)
    {
        const project_product_boot_restore_result_t restore_result =
            project_product_restore_boot();
        ui_boot_loading_note_restore_started(
            restore_result != PROJECT_PRODUCT_BOOT_RESTORE_FAILED);
    }

    if ((wake_flags & CONTROL_RT_WAKE_STREAM_RELEASE) != 0U)
        sample_stream_admission_control_service_releases();

    if ((wake_flags & CONTROL_RT_WAKE_HALL) != 0U)
        hall_keyboard_bridge_process();
    if ((wake_flags & CONTROL_RT_WAKE_ENCODER) != 0U)
        (void)encoder_control_dispatcher_service();
    if ((wake_flags & CONTROL_RT_WAKE_MIDI) != 0U)
    {
        midi_clock_service_pending();
        midi_control_poll();
        midi_host_control_poll_bounded(8U);
        seq_runtime_live_rec_drain_effective();
        if (seq_clock_bridge_is_external_source(seq_runtime_get_clock_source()) != 0U)
        {
            /* External clock is the existing cause for the real boundary. */
            seq_runtime_time_adapter_process();
            pattern_live_service();
        }
    }
    if ((wake_flags & CONTROL_RT_WAKE_UI) != 0U)
        control_domain_process_ui_messages();
    if ((wake_flags & CONTROL_RT_WAKE_STORAGE) != 0U)
    {
        control_domain_process_storage_messages();
        brick6_app_control_process_storage_completions();
        project_product_control_process();
        patch_product_control_process();
        pattern_live_control_process();
        project_load_quiesce_control_process();
        pattern_live_service();
    }
    if ((wake_flags & CONTROL_RT_WAKE_DEADLINE) != 0U)
    {
        seq_runtime_time_adapter_process();
        project_load_quiesce_control_process();
        pattern_live_service();
    }
    if ((wake_flags & CONTROL_RT_WAKE_LATEST) != 0U)
        brick6_master_control_process();

    /* Boot capture is a sampled/latest-value concern, never a global poll. */
    if ((g_boot_audio_state == BRICK6_BOOT_WAIT_MASTER)
        && ((wake_flags & (CONTROL_RT_WAKE_HALL
                           | CONTROL_RT_WAKE_LATEST
                           | CONTROL_RT_WAKE_DEADLINE)) != 0U))
    {
        if (brick6_master_control_boot_capture() != 0U)
        {
            brick6_master_control_boot_publish();
            g_boot_audio_state = BRICK6_BOOT_AUDIO_RUNNING;
        }
    }
    else if ((g_boot_audio_state == BRICK6_BOOT_AUDIO_RUNNING)
             && ((wake_flags & CONTROL_RT_WAKE_LATEST) != 0U))
    {
        brick6_master_control_process();
    }

    seq_runtime_control_deadline_service();
}

void brick6_app_usb_process(void)
{
    board_usb_host_process();
    midi_host_transport_poll_bounded(8U);
    midi_usb_service_poll();
}

void brick6_app_ui_process_input(void)
{
    ui_tasklet_initialize();
    ui_active_track_sync_process_pending();
    if (ui_boot_loading_is_active() == 0U)
        ui_core_service_track_selection_inputs();
    ui_tasklet_process_input();
    if (ui_event_pending_count() == 0U)
        ui_hall_mode_flow_service_pending(HAL_GetTick());
}

void brick6_app_ui_process_presentation(uint8_t deadline_due)
{
    ui_active_track_sync_process_pending();
    if (ui_event_pending_count() == 0U)
        ui_hall_mode_flow_service_pending(HAL_GetTick());
    ui_tasklet_process_presentation(deadline_due);
    if (ui_tasklet_is_initialized() == 0U)
        return;
    if (deadline_due != 0U)
        ui_renderer_oled_service_deadline();
    ui_active_track_sync_process_pending();
    ui_renderer_oled_service_render();
}
