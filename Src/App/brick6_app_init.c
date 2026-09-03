/**
 * @file brick6_app_init.c
 */

#include "App/brick6_app_init.h"

#include "App/control_domain.h"
#include "IPC/live_clock_control.h"
#include "App/control_clipboard.h"
#include "App/engine_tasklet.h"
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
#include "App/brick6_master_control.h"
#include "Storage/brick6_stream_service_task.h"
#include "Storage/boot_context_flash.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/pattern_load_storage.h"
#include "Storage/patch_product.h"
#include "Storage/project_control.h"
#include "Storage/project_product.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/storage_catalog.h"
#include "Storage/sd_preview.h"
#include "Storage/audio_recorder.h"
#include "Storage/waveform_cache.h"
#include "Storage/wav_convert.h"
#include "Platform/brick6_sd_config.h"

#include "App/Hall/hall_keyboard_bridge.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_loop.h"
#include "Seq/seq_runtime.h"
#include "UI/ui_active_track_sync.h"
#include "UI/display_flush_service.h"
#include "UI/ui_boot_loading.h"
#include "UI/ui_core.h"
#include "UI/ui_event.h"
#include "UI/ui_renderer_oled.h"
#include "UI/ui_tasklet.h"

typedef enum
{
    BRICK6_BOOT_WAIT_MASTER = 0,
    BRICK6_BOOT_AUDIO_RUNNING,
    BRICK6_BOOT_AUDIO_FAILED
} brick6_boot_audio_state_t;

static brick6_boot_audio_state_t g_boot_audio_state;

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


static void brick6_app_service_storage(void)
{
    project_load_quiesce_storage_retire();
    storage_catalog_service();
    wav_loader_catalog_storage_service();
    audio_recorder_service();
    project_product_storage_request_service();
    project_product_save_service();
    project_product_load_service();
    patch_product_apply_service();
    patch_product_storage_request_service();
    sample_global_pool_storage_request_service();
    sampler_ram_pool_storage_request_service();
    wavetable_pool_storage_request_service();
    multi_sample_pool_storage_request_service();
    multi_sample_import_storage_request_service();
    multi_sample_import_storage_delete_service();
    multi_sample_load_storage_request_service();
    wav_convert_service(65536U);
    if (multi_sample_load_has_pending() != 0U)
    {
        multi_sample_service_load(0U);
    }
    else
    {
        multi_sample_pool_service_retire();
        sampler_ram_pool_service_retire();
        wavetable_pool_service_retire();
        sampler_ram_pool_load_async_service();
        wavetable_pool_load_async_service();
        sampler_ram_pool_waveform_service(BRICK6_STREAM_OTHER_SD_QUANTUM_FRAMES);
        multi_sample_service_load(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
        pattern_storage_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES / 2U);
        waveform_cache_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
        sd_preview_process();
    }
}

void brick6_app_storage_process(void)
{
    brick6_app_storage_init();
    brick6_stream_service_task_poll();
    brick6_app_service_storage();
    brick6_stream_service_task_poll();
}

void brick6_app_control_process(void)
{
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
    control_domain_process_track_intents();
    control_domain_process_routing_intents();
    control_domain_process_param_intents();
    control_domain_process_seq_intents();
    control_domain_process_mod_intents();
    control_domain_process_macro_intents();
    control_domain_process_asset_intents();
    control_clipboard_process();
    control_project_intent_t project_intent;
    while (control_domain_take_project(&project_intent) != 0U)
    {
        const uint8_t operation = (uint8_t)project_intent.operation + 1U;
        project_product_control_process_intent(operation, project_intent.slot);
    }
    control_patch_intent_t patch_intent;
    while (control_domain_take_patch(&patch_intent) != 0U)
    {
        patch_product_control_process_intent((uint8_t)patch_intent.operation,
                                             patch_intent.slot,
                                             patch_intent.target_mask,
                                             patch_intent.entity,
                                             patch_intent.name);
    }
    project_load_quiesce_control_process();
    ui_event_from_inputs();
    engine_tasklet_poll();
    /*
     * TIM12 only advances INTERNAL time.  CONTROL_RT consumes the adapter.
    */
    seq_runtime_time_adapter_process();
    project_product_control_process();
    patch_product_control_process();
    pattern_live_control_process();
    pattern_live_service();
    if (g_boot_audio_state == BRICK6_BOOT_WAIT_MASTER)
    {
        if (brick6_master_control_boot_capture() != 0U)
        {
            brick6_master_control_boot_publish();
            g_boot_audio_state = BRICK6_BOOT_AUDIO_RUNNING;
        }
    }
    else if (g_boot_audio_state == BRICK6_BOOT_AUDIO_RUNNING)
    {
        brick6_master_control_process();
    }

    hall_keyboard_bridge_process();
    midi_control_poll();
    midi_host_control_poll_bounded(8U);
}

void brick6_app_usb_process(void)
{
    board_usb_host_process();
    midi_host_transport_poll_bounded(8U);
    midi_usb_service_poll();
}

void brick6_app_ui_process(void)
{
    ui_boot_loading_service();

    if (ui_boot_loading_is_active() == 0U)
    {
        ui_core_service_track_selection_inputs();
    }

    ui_tasklet_poll();
    if (ui_tasklet_is_initialized() != 0U)
    {
        ui_renderer_oled_service_poll();
        display_flush_service_poll();
    }
}
