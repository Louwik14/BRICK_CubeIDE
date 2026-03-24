/**
 * @file brick6_audio_runtime.c
 * @brief Callback DSP runtime extrait de brick6_app_init.
 *
 * Rôle du module:
 * - Regrouper le traitement audio bloc (synth, sampler, mixer, taps recorder).
 *
 * Frontière:
 * - Ne fait pas l'init applicative globale.
 * - Ne gère pas la policy de boot.
 */

#include "brick6_audio_runtime.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "Audio/live_recorder.h"
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "Audio/sd_multitrack_recorder.h"
#include "Sampler/voice_manager.h"
#include "mixer.h"
#include "ui_core.h"

#define HALFPI_F 1.57079632679489661923f

static live_recorder_t *g_live_recorder = NULL;
static ui_track_type_t g_runtime_synth_type = UI_TRACK_TYPE_DX7;
static uint8_t g_runtime_track_enabled = 1U;

static uint8_t brick6_runtime_track_is_enabled(void)
{
    const uint8_t active_track = ui_get_active_track();
    return (ui_get_track_family(active_track) != UI_TRACK_FAMILY_OFF) ? 1U : 0U;
}

static ui_track_type_t brick6_get_runtime_synth_type(void)
{
    const uint8_t active_track = ui_get_active_track();

    if (ui_get_track_family(active_track) == UI_TRACK_FAMILY_SYNTH)
    {
        return ui_get_track_type(active_track);
    }

    return UI_TRACK_TYPE_DX7;
}

void brick6_audio_runtime_init(live_recorder_t *live_recorder)
{
    g_live_recorder = live_recorder;
    g_runtime_synth_type = UI_TRACK_TYPE_DX7;
    g_runtime_track_enabled = 1U;
}

void brick6_audio_runtime_dsp(StereoTrack *tracks,
                              uint32_t track_count,
                              uint32_t frames)
{
    const uint8_t runtime_track_enabled = brick6_runtime_track_is_enabled();
    const ui_track_type_t runtime_synth_type = brick6_get_runtime_synth_type();

    if (((runtime_track_enabled == 0U) && (g_runtime_track_enabled != 0U))
            || ((runtime_track_enabled != 0U) && (g_runtime_track_enabled == 0U))
            || ((runtime_track_enabled != 0U) && (runtime_synth_type != g_runtime_synth_type)))
    {
        microdexed_synth_all_notes_off();
        monob_synth_all_notes_off();
        g_runtime_synth_type = runtime_synth_type;
    }
    g_runtime_track_enabled = runtime_track_enabled;

    if((track_count > 3U) && (tracks[3].enabled != 0U))
    {
        if (runtime_track_enabled == 0U)
        {
            memset(tracks[3].L, 0, sizeof(float) * frames);
            memset(tracks[3].R, 0, sizeof(float) * frames);
        }
        else
        {
            static float synth_mono[AUDIO_BLOCK_SIZE];

            if (runtime_synth_type == UI_TRACK_TYPE_MONOB)
            {
                monob_synth_process_block(synth_mono, frames);
            }
            else
            {
                microdexed_synth_process_block(synth_mono, frames);
            }

            for(uint32_t i = 0U; i < frames; ++i)
            {
                tracks[3].L[i] = synth_mono[i];
                tracks[3].R[i] = synth_mono[i];
            }
        }
    }

    if((track_count > 0U) && (tracks[0].enabled != 0U))
    {
        voice_manager_process(tracks[0].L, tracks[0].R, frames);

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

    if(g_live_recorder != NULL)
    {
        live_recorder_write(g_live_recorder,
                            tracks[0].L,
                            tracks[0].R,
                            frames);
    }

    const float xfade = 0.0f;
    if((xfade > 0.0f) && (g_live_recorder != NULL))
    {
        static float recL[AUDIO_BLOCK_SIZE];
        static float recR[AUDIO_BLOCK_SIZE];

        const float gain_rec  = sinf(xfade * HALFPI_F);
        const float gain_live = cosf(xfade * HALFPI_F);

        live_recorder_read(g_live_recorder, recL, recR, frames);

        for(uint32_t i = 0U; i < frames; i++)
        {
            tracks[0].L[i] = (tracks[0].L[i] * gain_live) + (recL[i] * gain_rec);
            tracks[0].R[i] = (tracks[0].R[i] * gain_live) + (recR[i] * gain_rec);
        }
    }
}
