/**
 * @file brick6_app_init.c
 */

#include "App/brick6_app_init.h"

#include "App/control_domain.h"
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
#include "Storage/sd_preview.h"
#include "Storage/audio_recorder.h"
#include "Storage/waveform_cache.h"
#include "Platform/brick6_sd_config.h"

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
    audio_recorder_service();
    project_product_save_service();
    project_product_load_service();
    patch_product_apply_service();
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
        pattern_load_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES / 2U);
        waveform_cache_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
        sd_preview_process();
    }
}

void brick6_app_process(void)
{
    engine_tasklet_poll();
    brick6_stream_service_task_poll();
    audio_domain_background_poll(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
    /*
     * Seq runtime core is serviced from superloop for both clock domains.
     * TIM12 IRQ only advances INTERNAL time ticks.
     */
    seq_runtime_time_adapter_process();
    brick6_app_service_storage();
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

    brick6_stream_service_task_poll();
    ui_boot_loading_service();
    if (ui_boot_loading_is_active() != 0U)
    {
        hall_loop_process();
    }
    else
    {
        brick6_process_hall_ui_keyboard_chain();
    }

    midi_poll();
}
