/**
 * @file brick6_app_init.c
 */

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "brick6_app_init.h"

#include "engine_tasklet.h"
#include "midi.h"
#include "sai.h"
#include "sdmmc.h"
#include "sdram.h"
#include "stm32h7xx_hal.h"
#include "usb_host.h"
#include "usb_device.h"
#include "audio.h"
#include "audio_float.h"
#include "cs42448.h"
#include "mixer.h"
#include "fx_pool.h"
#include "param_store.h"
#include "control_events.h"
#include "Audio/juno_synth.h"

#include "Sampler/sample_pool.h"
#include "Sampler/voice_manager.h"
#include "Audio/live_recorder.h"
#include "Audio/live_recorder_config.h"
#include "Audio/recorder_transport.h"
#include "Audio/sd_multitrack_recorder.h"
#include "Storage/memory_layout.h"

#include "App/Hall/hall_loop.h"

#define HALFPI_F 1.57079632679489661923f

static float g_master_gain = 1.0f;

static volatile uint32_t g_brick6_app_process_call_count = 0U;

static AUDIO_COLD_SDRAM float g_live_recorder_buffer[LIVE_RECORDER_MAX_FRAMES * 2U];
static live_recorder_t g_live_recorder;


/* ============================================================
   DSP CALLBACK
   ============================================================ */

/**
 * @brief Point d'entrée my_dsp.
 *
 * Rôle:
 * - Traitement audio temps réel.
 *
 * @param tracks Tableau de pistes audio.
 * @param track_count Nombre de pistes.
 * @param frames Nombre d'échantillons par bloc.
 */
static void my_dsp(StereoTrack *tracks,
                   uint32_t track_count,
                   uint32_t frames)
{
    if((track_count > 3U) && (tracks[3].enabled != 0U))
    {
        static float juno_mono[AUDIO_BLOCK_SIZE];

        juno_synth_process_block(juno_mono, frames);

        for(uint32_t i = 0U; i < frames; ++i)
        {
            tracks[3].L[i] = juno_mono[i];
            tracks[3].R[i] = juno_mono[i];
        }
    }

    if((track_count > 0U) && (tracks[0].enabled != 0U))
    {
        voice_manager_process(tracks[0].L, tracks[0].R, frames);

        for(uint32_t i = 0U; i < frames; i++)
        {
            float l = tracks[0].L[i] * g_master_gain;
            float r = tracks[0].R[i] * g_master_gain;

            if(!isfinite(l) || !isfinite(r))
            {
                l = 0.0f;
                r = 0.0f;
            }

            tracks[0].L[i] = l;
            tracks[0].R[i] = r;
        }

        sd_recorder_capture_tap_block(
            SD_RECORDER_TAP_TRACK_RAW,
            0U,
            tracks[0].L,
            tracks[0].R,
            frames);
    }

    mixer_process(tracks, track_count, frames);

    if(track_count > 0U)
    {
        sd_recorder_capture_tap_block(
            SD_RECORDER_TAP_MASTER,
            0U,
            tracks[0].L,
            tracks[0].R,
            frames);
    }

    live_recorder_write(&g_live_recorder,
                        tracks[0].L,
                        tracks[0].R,
                        frames);

    static float recL[AUDIO_BLOCK_SIZE];
    static float recR[AUDIO_BLOCK_SIZE];

    const float xfade = 0.0f;

    /*
    Constant Power Crossfade

    Linear crossfades produce a volume dip at the center (-6 dB).
    Using sin/cos gains preserves constant perceived loudness.
    */

    const float gain_rec  = sinf(xfade * HALFPI_F);
    const float gain_live = cosf(xfade * HALFPI_F);

    live_recorder_read(&g_live_recorder, recL, recR, frames);

    for(uint32_t i = 0U; i < frames; i++)
    {
        tracks[0].L[i] = (tracks[0].L[i] * gain_live) + (recL[i] * gain_rec);
        tracks[0].R[i] = (tracks[0].R[i] * gain_live) + (recR[i] * gain_rec);
    }
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
    MX_USB_HOST_Init();

    CS42448_Init(0x48);

    mixer_init();
    fx_pool_init();

    (void)fx_pool_activate_slot(0U, FX_EQ3);
    (void)fx_pool_activate_slot(1U, FX_SAT);
    (void)fx_pool_activate_slot(2U, FX_DAISY_COMP);

    mixer_set_track_insert_slot(0U, 0U, 2);

    audio_float_set_postgain(1.0f);
    audio_float_set_output_compensation(1.0f);

    audio_tracks_init();

    sample_pool_init();

    sample_pool_load(0, "0:/Drum.wav");
    sample_pool_load(1, "0:/La ritournelle.wav");

    live_recorder_init(&g_live_recorder);

    live_recorder_set_buffer(
        &g_live_recorder,
        g_live_recorder_buffer,
        LIVE_RECORDER_MAX_FRAMES);

    live_recorder_set_loop_length(
        &g_live_recorder,
        LIVE_RECORDER_MAX_FRAMES);

    live_recorder_start_play(&g_live_recorder);

    juno_synth_init(48000.0f, AUDIO_BLOCK_SIZE);
    juno_synth_set_enabled(1U);

    recorder_transport_init();
    sd_recorder_init();

    voice_manager_init();

    /* Trigger immédiat pour tester la lecture RAM */
    voice_manager_trigger(0, 0.30f, 0.30f);
    voice_manager_trigger(1, 0.30f, 0.30f);

    mixer_set_master(2.0f);

    track_enable(0, 1U);
    track_enable(1, 1U);
    track_enable(2, 1U);
    track_enable(3, 1U);

    track_set_gain(0, 1.0f);
    track_set_gain(1, 1.0f);
    track_set_gain(2, 1.0f);
    track_set_gain(3, 1.0f);

    audio_init(&hsai_BlockA2, &hsai_BlockB2);
    audio_set_float_callback(my_dsp);

    engine_tasklet_init(48000);
    param_store_init();
    control_event_init();

    hall_loop_init();

    audio_start();

    HAL_Delay(200);

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
    static uint8_t last_transport_recording = 0U;

    g_brick6_app_process_call_count++;

    engine_tasklet_poll();

    hall_loop_process();

    recorder_transport_process();

    {
        const uint8_t transport_recording =
            recorder_transport_is_recording();

        if((transport_recording != 0U) &&
           (last_transport_recording == 0U))
        {
            live_recorder_start_record(&g_live_recorder);
            (void)sd_recorder_request_start();
        }
        else if((transport_recording == 0U) &&
                (last_transport_recording != 0U))
        {
            live_recorder_stop_record(&g_live_recorder);
            (void)sd_recorder_request_stop();
        }

        last_transport_recording = transport_recording;
    }

    voice_manager_service();

    /* Service writer SD hors IRQ */
    sd_recorder_writer_service();
}



