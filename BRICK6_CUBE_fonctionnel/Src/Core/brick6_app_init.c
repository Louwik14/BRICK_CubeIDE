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
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "ui_core.h"

#include "Sampler/voice_manager.h"
#include "Audio/live_recorder.h"
#include "Audio/live_recorder_config.h"
#include "Storage/memory_layout.h"
#include "brick6_audio_runtime.h"
#include "brick6_boot_defaults.h"
#include "brick6_boot_fx_policy.h"
#include "brick6_master_control.h"
#include "brick6_recorder_runtime.h"
#include "brick6_sampler_bootstrap.h"

#include "App/Hall/hall_loop.h"
#include "App/Hall/hall_juno_midi.h"

static AUDIO_COLD_SDRAM float g_live_recorder_buffer[LIVE_RECORDER_MAX_FRAMES * 2U];
static live_recorder_t g_live_recorder;


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
    MX_USB_HOST_Init();

    CS42448_Init(0x48);

    mixer_init();
    brick6_boot_fx_policy_init();

    audio_float_set_postgain(1.0f);
    audio_float_set_output_compensation(1.0f);

    audio_tracks_init();

    brick6_sampler_bootstrap_load_pool();

    brick6_recorder_runtime_boot_init(&g_live_recorder,
                                      g_live_recorder_buffer,
                                      LIVE_RECORDER_MAX_FRAMES);

    microdexed_synth_init(48000.0f, AUDIO_BLOCK_SIZE);
    microdexed_synth_set_enabled(1U);
    monob_synth_init(48000.0f);
    hall_juno_midi_init();

    brick6_sampler_bootstrap_init_voices();

    mixer_set_master(0.0f);

    track_enable(0, 1U);
    track_enable(1, 1U);
    track_enable(2, 1U);
    track_enable(3, 1U);

    brick6_audio_runtime_init(&g_live_recorder);

    audio_init(&hsai_BlockA2, &hsai_BlockB2);
    audio_set_float_callback(brick6_audio_runtime_dsp);

    engine_tasklet_init(48000);
    param_store_init();
    brick6_boot_apply_param_defaults();
    control_event_init();

    hall_loop_init();

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
    brick6_master_control_process();

    hall_loop_process();
    ui_core_service_track_selection_inputs();
    hall_juno_midi_process();

    brick6_recorder_runtime_process_transport(&g_live_recorder);

    voice_manager_service();

    midi_poll();
    midi_host_poll();

    /* Service writer SD hors IRQ */
    brick6_recorder_runtime_service_writer();
}
