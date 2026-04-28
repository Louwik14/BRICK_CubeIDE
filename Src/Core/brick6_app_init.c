/**
 * @file brick6_app_init.c
 */

#include "brick6_app_init.h"

#include "engine_tasklet.h"
#include "midi.h"
#include "midi_host.h"
#include "sai.h"
#include "sdram.h"
#include "stm32h7xx_hal.h"
#include "usb_host.h"
#include "usb_device.h"
#include "audio.h"
#include "audio_float.h"
#include "cs42448.h"
#include "mixer.h"
#include "param_store.h"
#include "control_events.h"
#include "cpu_load.h"
#include "Audio/drum_synth.h"
#include "ui_core.h"
#include "ui_page_manager.h"

#include "Sampler/voice_manager.h"
#include "Sampler/sample_cache.h"
#include "Audio/live_recorder.h"
#include "Audio/live_recorder_config.h"
#include "Storage/memory_layout.h"
#include "Core/brick6_master_buffer.h"
#include "Core/brick6_master_buffer_stretch.h"
#include "brick6_audio_runtime.h"
#include "brick6_boot_defaults.h"
#include "brick6_boot_fx_policy.h"
#include "brick6_master_control.h"
#include "brick6_plaits_runtime.h"
#include "brick6_recorder_runtime.h"
#include "brick6_sampler_runtime.h"
#include "brick6_sampler_bootstrap.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/project_v1.h"
#include "Storage/undo_v2.h"
#include "Storage/sd_access_gate.h"
#include "Storage/sd_preview.h"
#include "Core/brick6_sd_config.h"

#include "App/Hall/hall_keyboard_bridge.h"
#include "App/Hall/hall_calibration.h"
#include "App/Hall/hall_loop.h"
#include "Seq/seq_runtime.h"

SDRAM_RECORDER static float g_live_recorder_buffer[LIVE_RECORDER_MAX_FRAMES * 2U];
static live_recorder_t g_live_recorder;

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

    MX_USB_DEVICE_Init();
    //MX_USB_HOST_Init();

    CS42448_Init(0x48);

    mixer_init();
    brick6_boot_fx_policy_init();

    audio_float_set_postgain(1.0f);
    audio_float_set_output_compensation(1.0f);

    audio_tracks_init();

    sd_access_gate_init();
    sd_preview_init();

    brick6_sampler_bootstrap_load_pool();

    brick6_recorder_runtime_boot_init(&g_live_recorder,
                                      g_live_recorder_buffer,
                                      LIVE_RECORDER_MAX_FRAMES);
    brick6_master_buffer_init(&g_live_recorder,
                              g_live_recorder_buffer,
                              LIVE_RECORDER_MAX_FRAMES);

    drum_synth_init(48000.0f);
    hall_keyboard_bridge_init();

    brick6_sampler_bootstrap_init_voices();
    brick6_sampler_runtime_init();
    brick6_plaits_runtime_init();

    mixer_set_master(0.0f);

    brick6_audio_runtime_init(&g_live_recorder);

    audio_init(&hsai_BlockA2, &hsai_BlockB2);
    audio_set_float_callback(brick6_audio_runtime_dsp);

    engine_tasklet_init(48000);
    param_store_init();
    brick6_boot_apply_param_defaults();
    seq_runtime_init();
    ui_core_init();
    pattern_live_init();
    project_v1_init();
    (void)project_v1_restore_boot_context();
    undo_v2_init();
    control_event_init();

    hall_loop_init();
    if (hall_calibration_load() != 0U)
    {
        ui_page_set(UI_PAGE_TEMPLATE_CFG);
    }
    else
    {
        ui_page_set(UI_PAGE_CALIBRATION);
    }

    audio_start();

    HAL_Delay(200);

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
    /*
     * Seq runtime core is serviced from superloop for both clock domains.
     * TIM12 IRQ only advances INTERNAL time ticks.
     */
    seq_runtime_time_adapter_process();
    sample_cache_service(4096U);
    pattern_live_service();
    sd_preview_process();
    brick6_master_control_process();
    brick6_master_buffer_stretch_service_analysis();

    brick6_process_hall_ui_keyboard_chain();

    brick6_recorder_runtime_process_transport(&g_live_recorder);

    voice_manager_service();

    midi_poll();

    /* Service writer SD hors IRQ */
    brick6_recorder_runtime_service_writer();
}
