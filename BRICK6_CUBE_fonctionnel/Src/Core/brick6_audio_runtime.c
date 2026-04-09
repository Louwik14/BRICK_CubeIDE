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
#include <stdio.h>

#include "Audio/live_recorder.h"
#include "Audio/microdexed_synth.h"
#include "Audio/monob_synth.h"
#include "Audio/drum_synth.h"
#include "Audio/tb3_synth.h"
#include "Audio/sd_multitrack_recorder.h"
#include "Sampler/voice_manager.h"
#include "mixer.h"
#include "ui_core.h"
#include "Core/track_runtime.h"
#include "Mod/mod_lfo_v1.h"

#define HALFPI_F 1.57079632679489661923f
#ifndef SEQ_DEBUG_TRACK_BINDING
#define SEQ_DEBUG_TRACK_BINDING 0
#endif

#if SEQ_DEBUG_TRACK_BINDING
#define BRICK6_RT_LOG(...) printf(__VA_ARGS__)
#else
#define BRICK6_RT_LOG(...) do { } while (0)
#endif

static live_recorder_t *g_live_recorder = NULL;
static uint8_t g_runtime_track_enabled = 1U;
static uint8_t g_runtime_last_monob_processed = 0xFFU;
static uint8_t g_runtime_last_tb3_processed = 0xFFU;
static uint8_t g_runtime_last_drum_processed = 0xFFU;
static uint8_t g_runtime_last_dx7_tracks = 0xFFU;
static uint8_t g_runtime_last_ui_active_track = 0xFFU;

typedef struct
{
    uint8_t monob_tracks;
    uint8_t tb3_tracks;
    uint8_t drum_tracks;
    uint8_t dx7_tracks;
} brick6_synth_usage_t;

static void brick6_collect_runtime_synth_usage(brick6_synth_usage_t *out_usage)
{
    uint8_t monob_count = 0U;
    uint8_t tb3_count = 0U;
    uint8_t drum_count = 0U;
    uint8_t dx7_tracks = 0U;

    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
        {
            continue;
        }

        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
        {
            monob_count++;
        }
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_TB3)
        {
            tb3_count++;
        }
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
        {
            drum_count++;
        }
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
        {
            dx7_tracks++;
        }
    }

    if (out_usage != NULL)
    {
        out_usage->monob_tracks = monob_count;
        out_usage->tb3_tracks = tb3_count;
        out_usage->drum_tracks = drum_count;
        out_usage->dx7_tracks = dx7_tracks;
    }
}

static drum_model_id_t brick6_map_runtime_type_to_drum_model(uint8_t runtime_type)
{
    switch ((track_runtime_type_t)runtime_type)
    {
        case TRACK_RUNTIME_TYPE_DRUM_TRX_BD:
            return DRUM_MODEL_ID_TRX_BD;
        case TRACK_RUNTIME_TYPE_DRUM_TRX_CLAVES:
            return DRUM_MODEL_ID_TRX_CLAVES;
        case TRACK_RUNTIME_TYPE_DRUM_TRX_HIHAT:
            return DRUM_MODEL_ID_TRX_HIHAT;
        case TRACK_RUNTIME_TYPE_DRUM_TRX_SNARE:
            return DRUM_MODEL_ID_TRX_SNARE;
        case TRACK_RUNTIME_TYPE_DRUM_FM_KICK:
            return DRUM_MODEL_ID_FM_KICK;
        case TRACK_RUNTIME_TYPE_DRUM_FM_SNARE:
            return DRUM_MODEL_ID_FM_SNARE;
        case TRACK_RUNTIME_TYPE_DRUM_FM_TOM:
            return DRUM_MODEL_ID_FM_TOM;
        case TRACK_RUNTIME_TYPE_DRUM_FM_RIMSHOT:
            return DRUM_MODEL_ID_FM_RIMSHOT;
        case TRACK_RUNTIME_TYPE_DRUM_FM_CLAP:
            return DRUM_MODEL_ID_FM_CLAP;
        case TRACK_RUNTIME_TYPE_DRUM_FM_COWBELL:
            return DRUM_MODEL_ID_FM_COWBELL;
        case TRACK_RUNTIME_TYPE_DRUM_FM_CYMBAL:
            return DRUM_MODEL_ID_FM_CYMBAL;
        default:
            return DRUM_MODEL_ID_COUNT;
    }
}

static void brick6_render_synth_tracks(uint32_t frames,
                                       uint8_t *out_monob_tracks,
                                       uint8_t *out_tb3_tracks,
                                       uint8_t *out_drum_tracks,
                                       uint8_t *out_dx7_tracks)
{
    static float monob_tmp[AUDIO_BLOCK_SIZE];
    static float tb3_tmp[AUDIO_BLOCK_SIZE];
    static float drum_tmp[AUDIO_BLOCK_SIZE];
    static float dx7_tmp[AUDIO_BLOCK_SIZE];
    uint8_t monob_tracks = 0U;
    uint8_t tb3_tracks = 0U;
    uint8_t drum_tracks = 0U;
    uint8_t dx7_tracks = 0U;
    uint8_t dx7_rendered_once = 0U;
    uint8_t dx7_mix_track = 0xFFU;

    {
        const uint8_t active_track = ui_get_active_track();
        const track_runtime_ctx_t *const active_ctx = track_runtime_get_ctx(active_track);
        if ((active_ctx != NULL)
                && (active_ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (active_ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
                && (active_ctx->mix_track_id < MIXER_MAX_TRACKS))
        {
            dx7_mix_track = active_ctx->mix_track_id;
        }
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL)
                || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                || (ctx->mix_track_id >= MIXER_MAX_TRACKS))
        {
            continue;
        }

        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_MONOB)
        {
            monob_synth_process_block_for_instance(ctx->instance_id, monob_tmp, frames);
            mixer_submit_external_mono(ctx->mix_track_id, monob_tmp, frames);
            monob_tracks++;
            continue;
        }

        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_TB3)
        {
            tb3_synth_process_block_for_instance(ctx->instance_id, tb3_tmp, frames);
            mixer_submit_external_mono(ctx->mix_track_id, tb3_tmp, frames);
            tb3_tracks++;
            continue;
        }

        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
        {
            if ((dx7_mix_track == 0xFFU) && (ctx->mix_track_id < MIXER_MAX_TRACKS))
            {
                dx7_mix_track = ctx->mix_track_id;
            }

            if (dx7_rendered_once == 0U)
            {
                microdexed_synth_process_block(dx7_tmp, frames);
                dx7_rendered_once = 1U;
            }

            if (ctx->mix_track_id == dx7_mix_track)
            {
                mixer_submit_external_mono(ctx->mix_track_id, dx7_tmp, frames);
            }
            dx7_tracks++;
        }

        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
        {
            const drum_model_id_t model_id = brick6_map_runtime_type_to_drum_model(ctx->type);
            if (model_id == DRUM_MODEL_ID_COUNT)
            {
                continue;
            }

            if (drum_synth_set_model_for_instance(ctx->instance_id, model_id) == 0U)
            {
                continue;
            }

            drum_synth_process_block_for_instance(ctx->instance_id, drum_tmp, frames);
            mixer_submit_external_mono(ctx->mix_track_id, drum_tmp, frames);
            drum_tracks++;
            continue;
        }
    }

    if (out_monob_tracks != NULL)
    {
        *out_monob_tracks = monob_tracks;
    }

    if (out_tb3_tracks != NULL)
    {
        *out_tb3_tracks = tb3_tracks;
    }

    if (out_dx7_tracks != NULL)
    {
        *out_dx7_tracks = dx7_tracks;
    }
    if (out_drum_tracks != NULL)
    {
        *out_drum_tracks = drum_tracks;
    }
}

void brick6_audio_runtime_init(live_recorder_t *live_recorder)
{
    g_live_recorder = live_recorder;
    g_runtime_track_enabled = 1U;
}

void brick6_audio_runtime_dsp(StereoTrack *tracks,
                              uint32_t track_count,
                              uint32_t frames)
{
    brick6_synth_usage_t synth_usage = { 0U, 0U, 0U };
    brick6_collect_runtime_synth_usage(&synth_usage);
    const uint8_t synth_runtime_enabled = ((synth_usage.monob_tracks > 0U)
            || (synth_usage.tb3_tracks > 0U)
            || (synth_usage.drum_tracks > 0U)
            || (synth_usage.dx7_tracks > 0U)) ? 1U : 0U;

    if (((synth_runtime_enabled == 0U) && (g_runtime_track_enabled != 0U))
            || ((synth_runtime_enabled != 0U) && (g_runtime_track_enabled == 0U)))
    {
        microdexed_synth_all_notes_off();
        monob_synth_all_notes_off_all();
        tb3_synth_all_notes_off_all();
        drum_synth_all_notes_off_all();
    }
    g_runtime_track_enabled = synth_runtime_enabled;

    mixer_external_inputs_clear();
    if (synth_runtime_enabled != 0U)
    {
        uint8_t monob_processed = 0U;
        uint8_t tb3_processed = 0U;
        uint8_t drum_processed = 0U;
        uint8_t dx7_tracks = 0U;
        brick6_render_synth_tracks(frames, &monob_processed, &tb3_processed, &drum_processed, &dx7_tracks);

        if ((monob_processed != g_runtime_last_monob_processed)
                || (tb3_processed != g_runtime_last_tb3_processed)
                || (drum_processed != g_runtime_last_drum_processed)
                || (dx7_tracks != g_runtime_last_dx7_tracks)
                || (ui_get_active_track() != g_runtime_last_ui_active_track))
        {
            BRICK6_RT_LOG("[AUDIO][RT] monob_tracks=%u tb3_tracks=%u drum_tracks=%u dx7_tracks=%u ui_active=%u\r\n",
                          (unsigned)monob_processed,
                          (unsigned)tb3_processed,
                          (unsigned)drum_processed,
                          (unsigned)dx7_tracks,
                          (unsigned)ui_get_active_track());
            g_runtime_last_monob_processed = monob_processed;
            g_runtime_last_tb3_processed = tb3_processed;
            g_runtime_last_drum_processed = drum_processed;
            g_runtime_last_dx7_tracks = dx7_tracks;
            g_runtime_last_ui_active_track = ui_get_active_track();
        }
    }

    mod_lfo_v1_process_block(frames);

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
