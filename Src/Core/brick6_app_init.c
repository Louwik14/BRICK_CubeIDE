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
#include "Board/board_audio.h"
#include "Board/board_usb.h"
#include "mixer.h"
#include "param_store.h"
#include "cpu_load.h"
#include "Audio/drum_synth.h"
#include "ui_core.h"
#include "ui_boot_loading.h"
#include "ui_page_manager.h"
#include "lowcost_button_test_config.h"

#include "Sampler/voice_manager.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Sampler/sample_global_pool.h"
#include "Storage/memory_layout.h"
#include "brick6_audio_runtime.h"
#include "brick6_braids_runtime.h"
#include "brick6_looper_runtime.h"
#include "brick6_boot_defaults.h"
#include "brick6_boot_fx_policy.h"
#include "brick6_master_control.h"
#include "brick6_sampler_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_stream_service_task.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/track_mute.h"
#include "brick6_sampler_bootstrap.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/project_v1.h"
#include "Storage/kit_v1.h"
#include "Storage/patch_v1.h"
#include "Storage/undo_v2.h"
#include "Storage/sd_access_gate.h"
#include "Storage/sd_preview.h"
#include "Storage/looper_storage.h"
#include "Storage/multi_record_writer.h"
#include "Storage/waveform_cache.h"
#include "Storage/wav_loader.h"
#include "Core/brick_build_config.h"
#if BRICK_TEST_BUILD
#include "Storage/audio_test_csv.h"
#include "Storage/monkey_test_log.h"
#include "Core/audio_test_runner.h"
#include "Core/audio_test2.h"
#include "Core/crash_capsule.h"
#include "Core/monkey_test.h"
#endif
#include "Core/brick6_sd_config.h"
#include "Core/stream_calibration.h"

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

    mixer_init();
    brick6_boot_fx_policy_init();

    audio_float_set_postgain(1.0f);
    audio_float_set_output_compensation(1.0f);

    audio_tracks_init();

    sd_access_gate_init();
#if BRICK_TEST_BUILD
    audio_test_csv_init();
    monkey_test_log_init();
    audio_test_runner_init();
    audio_test2_init();
    monkey_test_init();
#endif
    waveform_cache_init();
#if !BRICK6_STREAM_CALIBRATION
    (void)waveform_cache_ensure_dirs();
#endif
    wav_loader_catalog_init_load();
    sd_preview_init();
    looper_storage_raw_init();
    (void)looper_storage_raw_validate();
    multi_record_writer_init();
    sample_page_cache_init();
    sample_global_pool_init();
    sampler_ram_pool_init();
    wavetable_pool_init();
    multi_sample_pool_init();

    brick6_sampler_bootstrap_load_pool();

    drum_synth_init(48000.0f);
    hall_keyboard_bridge_init();

    brick6_sampler_bootstrap_init_voices();
    brick6_sampler_runtime_init();
    brick6_looper_runtime_init();
    brick6_braids_runtime_init();
    brick6_stack_runtime_init();
    brick6_wave_runtime_init();
    mixer_set_master(0.0f);

    brick6_audio_runtime_init();

    audio_init();
    audio_set_float_callback(brick6_audio_runtime_dsp);

    engine_tasklet_init(48000);
    param_store_init();
    brick6_boot_apply_param_defaults();
    seq_runtime_init();
    track_mute_init();
    ui_core_init();
    pattern_live_init();
    patch_v1_init();
    kit_v1_init();
    project_v1_init();
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
#if BRICK6_STREAM_CALIBRATION
    ui_page_set(UI_PAGE_STREAM_CALIBRATION);
    brick6_stream_calibration_init();
#endif
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
void brick6_app_process(void)
{
    engine_tasklet_poll();
    brick6_stream_service_task_poll();
#if BRICK6_STREAM_CALIBRATION
    brick6_stream_calibration_process();
    brick6_stream_service_task_poll();
    return;
#endif
    /*
     * Seq runtime core is serviced from superloop for both clock domains.
     * TIM12 IRQ only advances INTERNAL time ticks.
     */
    seq_runtime_time_adapter_process();
    if (multi_sample_load_has_pending() != 0U)
    {
        multi_sample_service_load(0U);
    }
    else
    {
        multi_record_writer_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
#if BRICK_TEST_BUILD
        audio_test_csv_service();
        monkey_test_log_service();
        audio_test_runner_tick();
        audio_test2_service();
        monkey_test_tick();
#endif
        if (looper_storage_raw_export_is_active() != 0U)
        {
            looper_storage_raw_export_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
        }
        else
        {
            brick6_sampler_runtime_service();
            sampler_ram_pool_waveform_service(BRICK6_STREAM_OTHER_SD_QUANTUM_FRAMES);
            brick6_looper_runtime_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
            if (brick6_looper_runtime_has_pending_sd_work() == 0U)
            {
                looper_storage_raw_export_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
                multi_sample_service_load(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
            }
            pattern_load_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES / 2U);
            waveform_cache_service(BRICK6_STREAM_OTHER_SD_QUANTUM_BYTES);
            sd_preview_process();
        }
    }
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

    voice_manager_service();

    midi_poll();
}
