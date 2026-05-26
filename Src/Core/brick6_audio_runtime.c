/**
 * @file brick6_audio_runtime.c
 * @brief Callback DSP runtime extrait de brick6_app_init.
 *
 * Rôle du module:
 * - Regrouper le traitement audio bloc (synth, sampler, looper, mixer, master FX).
 *
 * Frontière:
 * - Ne fait pas l'init applicative globale.
 * - Ne gère pas la policy de boot.
 */

#include "brick6_audio_runtime.h"

#include <stddef.h>
#include <string.h>

#include "Audio/drum_synth.h"
#include "Audio/fx_master_macro.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Sampler/voice_manager.h"
#include "Storage/sd_preview.h"
#include "mixer.h"
#include "ui_core.h"
#include "Core/track_runtime.h"
#include "Mod/mod_lfo_v1.h"

static uint8_t g_runtime_track_enabled = 1U;
static uint8_t g_runtime_last_drum_processed = 0xFFU;
static uint8_t g_runtime_last_ui_active_track = 0xFFU;
typedef struct
{
    uint8_t drum_tracks;
} brick6_synth_usage_t;

static void brick6_collect_runtime_synth_usage(brick6_synth_usage_t *out_usage)
{
    uint8_t drum_count = 0U;

    (void)track_runtime_refresh_if_dirty();
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
        {
            continue;
        }

        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
        {
            drum_count++;
        }
    }

    if (out_usage != NULL)
    {
        out_usage->drum_tracks = drum_count;
    }
}

static drum_model_id_t brick6_map_runtime_type_to_drum_model(uint8_t runtime_type)
{
    switch ((track_runtime_type_t)runtime_type)
    {
        case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:
            return DRUM_MODEL_ID_BD_ANALOG;
        default:
            return DRUM_MODEL_ID_NONE;
    }
}

static void brick6_render_synth_tracks(uint32_t frames,
                                       uint8_t *out_drum_tracks)
{
    static float drum_tmp[AUDIO_BLOCK_SIZE];
    uint8_t drum_tracks = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL)
                || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                || (track_runtime_is_audio_routable(track) == 0U))
        {
            continue;
        }

        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
        {
            const drum_model_id_t model_id = brick6_map_runtime_type_to_drum_model(ctx->type);
            if ((model_id == DRUM_MODEL_ID_COUNT) || (model_id == DRUM_MODEL_ID_NONE))
            {
                (void)drum_synth_set_model_for_instance(ctx->instance_id, DRUM_MODEL_ID_NONE);
            }
            else if (drum_synth_set_model_for_instance(ctx->instance_id, model_id) == 0U)
            {
                continue;
            }

            drum_synth_process_block_for_instance(ctx->instance_id, drum_tmp, frames);
            mixer_submit_external_mono_native(ctx->mix_track_id, drum_tmp, frames);
            drum_tracks++;
            continue;
        }
    }

    if (out_drum_tracks != NULL)
    {
        *out_drum_tracks = drum_tracks;
    }
}

static void brick6_render_sampler_tracks(uint32_t frames, uint8_t *out_sampler_tracks)
{
    static float sampler_tmp_l[AUDIO_BLOCK_SIZE];
    static float sampler_tmp_r[AUDIO_BLOCK_SIZE];
    uint8_t sampler_tracks = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL)
                || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
                || (track_runtime_is_audio_routable(track) == 0U))
        {
            continue;
        }

        if ((brick6_sampler_runtime_track_has_active_ram_voice(ctx->track_id) != 0U)
                && ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_CLIP)
                && ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_MULTI))
        {
            float *direct_l = NULL;
            float *direct_r = NULL;
            if (mixer_begin_external_stereo(ctx->mix_track_id, frames, &direct_l, &direct_r) != 0U)
            {
                memset(direct_l, 0, frames * sizeof(float));
                memset(direct_r, 0, frames * sizeof(float));
                brick6_sampler_runtime_render_track(ctx, direct_l, direct_r, frames);
                mixer_commit_external_stereo(ctx->mix_track_id, frames);
                sampler_tracks++;
                continue;
            }
        }

        memset(sampler_tmp_l, 0, frames * sizeof(float));
        memset(sampler_tmp_r, 0, frames * sizeof(float));
        brick6_sampler_runtime_render_track(ctx, sampler_tmp_l, sampler_tmp_r, frames);
        mixer_submit_external_stereo(ctx->mix_track_id, sampler_tmp_l, sampler_tmp_r, frames);
        sampler_tracks++;
    }

    if (out_sampler_tracks != NULL)
    {
        *out_sampler_tracks = sampler_tracks;
    }
}

static void brick6_render_looper_tracks(uint32_t frames, uint8_t *out_looper_tracks)
{
    static float looper_tmp_l[AUDIO_BLOCK_SIZE];
    static float looper_tmp_r[AUDIO_BLOCK_SIZE];
    uint8_t looper_tracks = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL)
                || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_LOOPER)
                || (track_runtime_is_audio_routable(track) == 0U)
                || (brick6_looper_runtime_is_playing(track) == 0U))
        {
            continue;
        }

        memset(looper_tmp_l, 0, frames * sizeof(float));
        memset(looper_tmp_r, 0, frames * sizeof(float));
        brick6_looper_runtime_render_track(ctx, looper_tmp_l, looper_tmp_r, frames);
        mixer_submit_external_stereo(ctx->mix_track_id, looper_tmp_l, looper_tmp_r, frames);
        looper_tracks++;
    }

    if (out_looper_tracks != NULL)
    {
        *out_looper_tracks = looper_tracks;
    }
}

static void brick6_render_wave_tracks(uint32_t frames, uint8_t *out_wave_tracks)
{
    static float wave_tmp[AUDIO_BLOCK_SIZE];
    uint8_t wave_tracks = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL)
                || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
                || (track_runtime_is_audio_routable(track) == 0U))
        {
            continue;
        }

        brick6_braids_runtime_render_instance(ctx->instance_id, wave_tmp, frames);
        mixer_submit_external_mono_native(ctx->mix_track_id, wave_tmp, frames);
        wave_tracks++;
    }

    if (out_wave_tracks != NULL)
    {
        *out_wave_tracks = wave_tracks;
    }
}

void brick6_audio_runtime_init(void)
{
    g_runtime_track_enabled = 1U;
    fx_master_macro_init(48000.0f);
}

void brick6_audio_runtime_dsp(StereoTrack *tracks,
                              uint32_t track_count,
                              uint32_t frames)
{
    brick6_synth_usage_t synth_usage = { 0U };
    brick6_collect_runtime_synth_usage(&synth_usage);
    const uint8_t synth_runtime_enabled = (synth_usage.drum_tracks > 0U) ? 1U : 0U;

    if (((synth_runtime_enabled == 0U) && (g_runtime_track_enabled != 0U))
            || ((synth_runtime_enabled != 0U) && (g_runtime_track_enabled == 0U)))
    {
        drum_synth_all_notes_off_all();
    }
    g_runtime_track_enabled = synth_runtime_enabled;

    mixer_external_inputs_clear();
    mod_lfo_v1_process_block(frames);

    if (synth_runtime_enabled != 0U)
    {
        uint8_t drum_processed = 0U;
        brick6_render_synth_tracks(frames, &drum_processed);

        if ((drum_processed != g_runtime_last_drum_processed)
                || (ui_get_active_track() != g_runtime_last_ui_active_track))
        {
            g_runtime_last_drum_processed = drum_processed;
            g_runtime_last_ui_active_track = ui_get_active_track();
        }
    }

    {
        uint8_t sampler_tracks = 0U;
        brick6_render_sampler_tracks(frames, &sampler_tracks);
        (void)sampler_tracks;
    }

    {
        uint8_t looper_tracks = 0U;
        brick6_render_looper_tracks(frames, &looper_tracks);
        (void)looper_tracks;
    }

    {
        uint8_t wave_tracks = 0U;
        brick6_render_wave_tracks(frames, &wave_tracks);
        (void)wave_tracks;
    }

    if((track_count > 0U) && (tracks[0].enabled != 0U))
    {
        voice_manager_process(tracks[0].L, tracks[0].R, frames);
    }

    mixer_process(tracks, track_count, frames);

    if (track_count > 0U)
    {
        fx_master_macro_process_block(tracks[0].L, tracks[0].R, frames);
        (void)sd_preview_render_main(tracks[0].L, tracks[0].R, frames);
    }

}
