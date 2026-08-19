/**
 * @file brick6_app_init.c
 */

#include "brick6_app_init.h"

#include "engine_tasklet.h"
#include "midi.h"
#include "midi_host.h"
#include "sdram.h"
#include "audio.h"
#include "audio_float.h"
#include "Audio/brick6_audio_boot.h"
#include "Board/board_audio.h"
#include "Board/board_usb.h"
#include "param_store.h"
#include "cpu_load.h"
#include "ui_core.h"
#include "ui_boot_loading.h"
#include "ui_page_manager.h"
#include "lowcost_button_test_config.h"

#include "Sampler/sample_cache.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Sampler/sample_global_pool.h"
#include "Storage/memory_layout.h"
#include "brick6_audio_runtime.h"
#include "brick6_looper_runtime.h"
#include "brick6_boot_defaults.h"
#include "brick6_boot_fx_policy.h"
#include "brick6_master_control.h"
#include "brick6_sampler_runtime.h"
#include "Core/brick6_stream_service_task.h"
#include "Core/track_mute.h"
#include "Core/project_control.h"
#include "brick6_sampler_bootstrap.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/project_product.h"
#include "Storage/patch_product.h"
#include "Storage/undo_v2.h"
#include "Storage/sd_access_gate.h"
#include "Storage/sd_preview.h"
#include "Storage/audio_recorder.h"
#include "Storage/waveform_cache.h"
#include "Storage/wav_loader.h"
#include "Core/brick_build_config.h"
#if BRICK_TEST_BUILD
#include "Core/crash_capsule.h"
#endif
#include "Core/brick6_sd_config.h"

#include "App/Hall/hall_keyboard_bridge.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_loop.h"
#include "Seq/seq_runtime.h"

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
#if BRICK_TEST_BUILD
    (void)crash_capsule_init();
#endif
    SDRAM_Init();

    board_usb_device_init();
    //MX_USB_HOST_Init();

    board_audio_codec_init();

    static const brick6_audio_boot_intent_t audio_boot = {
        .sample_rate_hz = 48000.0f,
        .master_gain = 0.0f,
        .postgain = 1.0f,
        .output_compensation = 1.0f,
        .fx_slot_count = BRICK6_AUDIO_BOOT_FX_SLOT_COUNT,
        .fx_slots = {
            { .slot = 0U, .type = (uint8_t)BRICK6_AUDIO_BOOT_FX_EQ3 },
            { .slot = 2U, .type = (uint8_t)BRICK6_AUDIO_BOOT_FX_COMP_LAB },
        },
    };
    (void)brick6_audio_boot_apply_early(&audio_boot);
    brick6_boot_fx_policy_init();
    (void)brick6_audio_boot_apply_output_tracks(&audio_boot);

    sd_access_gate_init();
#if BRICK_TEST_BUILD
#endif
    waveform_cache_init();
    (void)waveform_cache_ensure_dirs();
    wav_loader_catalog_init_load();
    sd_preview_init();
    sample_page_cache_init();
    sample_global_pool_init();
    sampler_ram_pool_init();
    wavetable_pool_init();
    multi_sample_pool_init();

    brick6_sampler_bootstrap_load_pool();
    audio_recorder_init();

    (void)brick6_audio_boot_apply_drum(&audio_boot);
    hall_keyboard_bridge_init();

    brick6_sampler_runtime_init();
    brick6_looper_runtime_init();
    (void)brick6_audio_boot_apply_engines(&audio_boot);
    brick6_audio_boot_apply_binding_io();
    audio_set_float_callback(brick6_audio_runtime_dsp);

    engine_tasklet_init(48000);
    param_store_init();
    seq_runtime_init();
    track_mute_init();
    ui_core_init();
    param_set(PARAM_MASTER_GAIN, audio_boot.master_gain);
    param_set(PARAM_POST_GAIN, audio_boot.postgain);
    param_set(PARAM_OUTPUT_COMP, audio_boot.output_compensation);
    brick6_boot_apply_param_defaults();
    project_control_init();
    pattern_live_init();
    patch_product_init();
    project_product_init();
    ui_boot_loading_begin();
    undo_v2_init();
    hall_loop_init();
    if (hall_calibration_load() != 0U)
    {
        ui_page_set(UI_PAGE_TEMPLATE_CFG);
    }
    else
    {
        ui_page_set(UI_PAGE_CALIBRATION);
    }
#if LOWCOST_BUTTON_TEST_PAGE
    ui_page_set(UI_PAGE_LOWCOST_BUTTON_TEST);
#endif
    brick6_stream_service_task_init();
    (void)audio_start();

    cpu_load_reset_peak();

    midi_init();


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
    if (multi_sample_load_has_pending() != 0U)
    {
        multi_sample_service_load(0U);
    }
    else
    {
#if BRICK_TEST_BUILD
#endif
        brick6_sampler_runtime_service();
        sampler_ram_pool_waveform_service(BRICK6_STREAM_OTHER_SD_QUANTUM_FRAMES);
        brick6_looper_runtime_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
        if (brick6_looper_runtime_has_pending_sd_work() == 0U)
        {
            multi_sample_service_load(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
        }
        pattern_load_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES / 2U);
        waveform_cache_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
        sd_preview_process();
    }
}

void brick6_app_process(void)
{
    engine_tasklet_poll();
    brick6_stream_service_task_poll();
    /*
     * Seq runtime core is serviced from superloop for both clock domains.
     * TIM12 IRQ only advances INTERNAL time ticks.
     */
    seq_runtime_time_adapter_process();
    brick6_app_service_storage();
    pattern_live_service();
    brick6_master_control_process();

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
