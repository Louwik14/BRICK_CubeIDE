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
#include "Audio/tb3_synth.h"
#include "Audio/sd_multitrack_recorder.h"
#include "Sampler/voice_manager.h"
#include "mixer.h"
#include "ui_core.h"
#include "Core/track_runtime.h"

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
static uint8_t g_runtime_last_dx7_tracks = 0xFFU;
static uint8_t g_runtime_last_ui_active_track = 0xFFU;

typedef struct
{
    uint8_t monob_tracks;
    uint8_t tb3_tracks;
    uint8_t dx7_tracks;
} brick6_synth_usage_t;

static void brick6_collect_runtime_synth_usage(brick6_synth_usage_t *out_usage)
{
    uint8_t monob_count = 0U;
    uint8_t tb3_count = 0U;
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
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7)
        {
            dx7_tracks++;
        }
    }

    if (out_usage != NULL)
    {
        out_usage->monob_tracks = monob_count;
        out_usage->tb3_tracks = tb3_count;
        out_usage->dx7_tracks = dx7_tracks;
    }
}

static void brick6_synth_bus_clear(float *bus, uint32_t frames)
{
    memset(bus, 0, sizeof(float) * frames);
}

static void brick6_synth_bus_accumulate(float *dst, const float *src, uint32_t frames)
{
    for (uint32_t i = 0U; i < frames; ++i)
    {
        dst[i] += src[i];
    }
}

static uint8_t brick6_render_monob_instances(float *mix_bus, uint32_t frames)
{
    uint8_t processed = 0U;
    static float mono_tmp[AUDIO_BLOCK_SIZE];

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL)
                || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_MONOB))
        {
            continue;
        }

        monob_synth_process_block_for_instance(ctx->instance_id, mono_tmp, frames);
        brick6_synth_bus_accumulate(mix_bus, mono_tmp, frames);
        processed++;
    }

    return processed;
}

static uint8_t brick6_render_dx7_instances(float *mix_bus, uint32_t frames)
{
    static float dx7_tmp[AUDIO_BLOCK_SIZE];
    uint8_t dx7_tracks = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx != NULL)
                && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DX7))
        {
            dx7_tracks++;
        }
    }

    if (dx7_tracks > 0U)
    {
        microdexed_synth_process_block(dx7_tmp, frames);
        brick6_synth_bus_accumulate(mix_bus, dx7_tmp, frames);
    }

    return dx7_tracks;
}

static uint8_t brick6_render_tb3_instances(float *mix_bus, uint32_t frames)
{
    uint8_t processed = 0U;
    static float tb3_tmp[AUDIO_BLOCK_SIZE];

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL)
                || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_TB3))
        {
            continue;
        }

        tb3_synth_process_block_for_instance(ctx->instance_id, tb3_tmp, frames);
        brick6_synth_bus_accumulate(mix_bus, tb3_tmp, frames);
        processed++;
    }

    return processed;
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
            || (synth_usage.dx7_tracks > 0U)) ? 1U : 0U;

    if (((synth_runtime_enabled == 0U) && (g_runtime_track_enabled != 0U))
            || ((synth_runtime_enabled != 0U) && (g_runtime_track_enabled == 0U)))
    {
        microdexed_synth_all_notes_off();
        monob_synth_all_notes_off_all();
        tb3_synth_all_notes_off_all();
    }
    g_runtime_track_enabled = synth_runtime_enabled;

    if((track_count > 3U) && (tracks[3].enabled != 0U))
    {
        if (synth_runtime_enabled == 0U)
        {
            memset(tracks[3].L, 0, sizeof(float) * frames);
            memset(tracks[3].R, 0, sizeof(float) * frames);
        }
        else
        {
            static float synth_mono[AUDIO_BLOCK_SIZE];
            brick6_synth_bus_clear(synth_mono, frames);
            const uint8_t monob_processed = brick6_render_monob_instances(synth_mono, frames);
            const uint8_t tb3_processed = brick6_render_tb3_instances(synth_mono, frames);
            const uint8_t dx7_tracks = brick6_render_dx7_instances(synth_mono, frames);

            if ((monob_processed != g_runtime_last_monob_processed)
                    || (tb3_processed != g_runtime_last_tb3_processed)
                    || (dx7_tracks != g_runtime_last_dx7_tracks)
                    || (ui_get_active_track() != g_runtime_last_ui_active_track))
            {
                BRICK6_RT_LOG("[AUDIO][RT] monob_processed=%u tb3_processed=%u dx7_tracks=%u ui_active=%u\r\n",
                              (unsigned)monob_processed,
                              (unsigned)tb3_processed,
                              (unsigned)dx7_tracks,
                              (unsigned)ui_get_active_track());
                g_runtime_last_monob_processed = monob_processed;
                g_runtime_last_tb3_processed = tb3_processed;
                g_runtime_last_dx7_tracks = dx7_tracks;
                g_runtime_last_ui_active_track = ui_get_active_track();
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
