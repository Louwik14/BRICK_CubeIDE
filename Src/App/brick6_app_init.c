/**
 * @file brick6_app_init.c
 */

#include "App/brick6_app_init.h"

#include "App/control_domain.h"
#include "App/brick6_boot_fx_policy.h"
#include "App/engine_tasklet.h"
#include "Audio/audio_domain.h"
#include "midi.h"
#include "midi_host.h"
#include "sdram.h"
#include "ui_core.h"
#include "ui_boot_loading.h"

#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "App/brick6_master_control.h"
#include "Storage/brick6_stream_service_task.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/patch_product.h"
#include "Storage/project_product.h"
#include "Storage/project_control.h"
#include "Storage/sd_preview.h"
#include "Storage/audio_recorder.h"
#include "Storage/waveform_cache.h"
#include "Platform/brick6_sd_config.h"
#include "Platform/idle_latency_diag.h"

#include "App/Hall/hall_keyboard_bridge.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_loop.h"
#include "Seq/seq_runtime.h"
#include "UI/ui_active_track_sync.h"

typedef enum
{
    BRICK6_BOOT_WAIT_MASTER = 0,
    BRICK6_BOOT_AUDIO_RUNNING,
    BRICK6_BOOT_AUDIO_FAILED
} brick6_boot_audio_state_t;

static brick6_boot_audio_state_t g_boot_audio_state;

static void brick6_process_hall_ui_keyboard_chain(void)
{
    /*
     * Ordering contract (do not reorder):
     * 1) hall_loop_process()
     * 2) ui_core_service_track_selection_inputs()
     * 3) hall_keyboard_bridge_process()
     *
     * ui_core must consume track-selection and hall-mode side effects before
     * hall->keyboard injection runs in the same superloop cycle.
     */
    hall_loop_process();
    ui_core_service_track_selection_inputs();
    hall_keyboard_bridge_process();
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
    brick6_boot_fx_policy_init();
    control_domain_start(audio_boot.postgain, audio_boot.output_compensation);
    g_boot_audio_state = BRICK6_BOOT_WAIT_MASTER;
}


/* ============================================================
   SUPERLOOP
   ============================================================ */

/**
 * @brief Point d'entrée brick6_app_process.
 *
 * Rôle:
 * - Boucle principale applicative.
 */
static void brick6_app_service_storage(void)
{
    uint32_t started = idle_latency_diag_begin();
    audio_recorder_service();
    idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_RECORDER, started);
    started = idle_latency_diag_begin();
    project_product_save_service();
    idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_PROJECT_SAVE, started);
    started = idle_latency_diag_begin();
    project_product_load_service();
    idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_PROJECT_LOAD, started);
    started = idle_latency_diag_begin();
    patch_product_apply_service();
    idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_PATCH, started);
    if (multi_sample_load_has_pending() != 0U)
    {
        started = idle_latency_diag_begin();
        multi_sample_service_load(0U);
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_MULTI_PRIORITY,
                                      started);
    }
    else
    {
        started = idle_latency_diag_begin();
        multi_sample_pool_service_retire();
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_MULTI_RETIRE,
                                      started);
        started = idle_latency_diag_begin();
        sampler_ram_pool_service_retire();
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_RAM_RETIRE, started);
        started = idle_latency_diag_begin();
        wavetable_pool_service_retire();
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_WAVETABLE_RETIRE,
                                      started);
        started = idle_latency_diag_begin();
        sampler_ram_pool_load_async_service();
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_RAM_LOADER, started);
        started = idle_latency_diag_begin();
        wavetable_pool_load_async_service();
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_WAVETABLE_LOADER,
                                      started);
        started = idle_latency_diag_begin();
        project_control_asset_load_service();
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_PROJECT_ASSET,
                                      started);
        started = idle_latency_diag_begin();
        sampler_ram_pool_waveform_service(BRICK6_STREAM_OTHER_SD_QUANTUM_FRAMES);
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_RAM_WAVEFORM,
                                      started);
        started = idle_latency_diag_begin();
        multi_sample_service_load(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_MULTI, started);
        started = idle_latency_diag_begin();
        pattern_load_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES / 2U);
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_PATTERN, started);
        started = idle_latency_diag_begin();
        waveform_cache_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_WAVEFORM_CACHE,
                                      started);
        started = idle_latency_diag_begin();
        sd_preview_process();
        idle_latency_storage_diag_end(IDLE_LATENCY_STORAGE_PREVIEW, started);
    }
}

void brick6_app_process(void)
{
    uint32_t diag_started = idle_latency_diag_begin();
    engine_tasklet_poll();
    idle_latency_diag_end(IDLE_LATENCY_SERVICE_ENGINE_TICK, diag_started);
    diag_started = idle_latency_diag_begin();
    brick6_stream_service_task_poll();
    idle_latency_diag_end(IDLE_LATENCY_SERVICE_STREAM, diag_started);
    diag_started = idle_latency_diag_begin();
    audio_domain_background_poll(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
    idle_latency_diag_end(IDLE_LATENCY_SERVICE_AUDIO_BG_LOCAL, diag_started);
    /*
     * Seq runtime core is serviced from superloop for both clock domains.
     * TIM12 IRQ only advances INTERNAL time ticks.
     */
    diag_started = idle_latency_diag_begin();
    seq_runtime_time_adapter_process();
    idle_latency_diag_end(IDLE_LATENCY_SERVICE_SEQ, diag_started);
    diag_started = idle_latency_diag_begin();
    brick6_app_service_storage();
    idle_latency_diag_end(IDLE_LATENCY_SERVICE_STORAGE, diag_started);
    diag_started = idle_latency_diag_begin();
    pattern_live_service();
    if (g_boot_audio_state == BRICK6_BOOT_WAIT_MASTER)
    {
        if (brick6_master_control_boot_capture() != 0U)
        {
            if (audio_domain_start() != 0U)
            {
                brick6_master_control_boot_publish();
                g_boot_audio_state = BRICK6_BOOT_AUDIO_RUNNING;
            }
            else
            {
                g_boot_audio_state = BRICK6_BOOT_AUDIO_FAILED;
            }
        }
    }
    else if (g_boot_audio_state == BRICK6_BOOT_AUDIO_RUNNING)
    {
        brick6_master_control_process();
    }
    idle_latency_diag_end(IDLE_LATENCY_SERVICE_CONTROL, diag_started);
    diag_started = idle_latency_diag_begin();
    brick6_stream_service_task_poll();
    idle_latency_diag_end(IDLE_LATENCY_SERVICE_STREAM, diag_started);
    ui_boot_loading_service();
    diag_started = idle_latency_diag_begin();
    if (ui_boot_loading_is_active() != 0U)
    {
        hall_loop_process();
    }
    else
    {
        brick6_process_hall_ui_keyboard_chain();
    }
    midi_poll();
    idle_latency_diag_end(IDLE_LATENCY_SERVICE_HALL_MIDI, diag_started);
}
