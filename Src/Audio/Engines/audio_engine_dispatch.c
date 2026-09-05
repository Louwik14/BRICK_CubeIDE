/**
 * @file audio_engine_dispatch.c
 * @brief Callback DSP runtime extrait de brick6_app_init.
 *
 * RÃ´le du module:
 * - Regrouper le traitement audio bloc (synth, sampler, looper, mixer, master FX).
 *
 * FrontiÃ¨re:
 * - Ne fait pas l'init applicative globale.
 * - Ne gÃ¨re pas la policy de boot.
 */

#include "Audio/Engines/audio_engine_dispatch.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Platform/memory_layout.h"

#include <stddef.h>
#include <string.h>

#include "Audio/drum_synth.h"
#include "Audio/audio_io.h"
#include "Audio/metronome_runtime.h"
#include "Audio/synth_waveform_audio.h"
#include "Audio/Engines/prism_engine.h"
#include "Audio/brick6_looper_runtime.h"
#include "Audio/Engines/Sampler/brick6_sampler_runtime.h"
#include "Audio/Engines/stack_engine.h"
#include "Audio/Engines/wavetable_engine.h"
#include "Audio/Engines/fm_engine.h"
#include "Track/synth_polyphony.h"
#include "Audio/sd_preview_audio.h"
#include "mixer.h"
#include "Track/track_types.h"
#include "Mod/mod_lfo_v1_audio.h"
#include "Mod/mod_matrix_audio.h"

static uint8_t g_runtime_track_enabled = 1U;
static uint8_t g_runtime_last_drum_processed = 0xFFU;
/* Unowned physical sources must never default to entity 0: publication is
 * first-writer-wins per mixer lane, so a zeroed MIC lane would mask USB. */
static uint8_t g_audio_input_owner[ENTITY_TOPOLOGY_AUDIO_SOURCE_COUNT] = {
    [ENTITY_AUDIO_SOURCE_LINE] = BRICK_ENTITY_INVALID_ID,
    [ENTITY_AUDIO_SOURCE_MIC] = BRICK_ENTITY_INVALID_ID,
    [ENTITY_AUDIO_SOURCE_USB] = BRICK_ENTITY_INVALID_ID
};

uint8_t brick6_audio_runtime_set_input_owner(uint8_t input, uint8_t owner)
{
    if (input >= ENTITY_TOPOLOGY_AUDIO_SOURCE_COUNT) return 0U;
    g_audio_input_owner[input] = owner;
    return 1U;
}

static void brick6_publish_owned_external_source(uint8_t source,
                                                  const float *left,
                                                  const float *right,
                                                  uint32_t frames)
{
    const uint8_t owner = g_audio_input_owner[source];
    if (owner >= BRICK_ENTITY_CAPACITY)
    {
        return;
    }

    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(owner, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    if ((ctx == NULL)
            || ((track_runtime_family_t)ctx->family != TRACK_RUNTIME_FAMILY_EXTERNAL)
            || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
            || (ctx->program_route.mix_track_id >= MIXER_MAX_TRACKS))
    {
        return;
    }

    mixer_submit_external_stereo(ctx->program_route.mix_track_id,
                                 left,
                                 right,
                                 frames);
}

static void brick6_publish_owned_external_sources(uint32_t frames)
{
    const audio_physical_inputs_t *const inputs =
        audio_io_get_current_physical_inputs();

    brick6_publish_owned_external_source(ENTITY_AUDIO_SOURCE_LINE,
                                         inputs->line.left,
                                         inputs->line.right,
                                         frames);
    brick6_publish_owned_external_source(ENTITY_AUDIO_SOURCE_MIC,
                                         inputs->mic.mono,
                                         inputs->mic.mono,
                                         frames);
    brick6_publish_owned_external_source(ENTITY_AUDIO_SOURCE_USB,
                                         inputs->usb.left,
                                         inputs->usb.right,
                                         frames);
}

static drum_model_id_t brick6_map_runtime_type_to_drum_model(uint8_t runtime_type)
{
    switch ((track_runtime_type_t)runtime_type)
    {
        case TRACK_RUNTIME_TYPE_DRUM_MD:
            return DRUM_MODEL_ID_MD;
        case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:
            return DRUM_MODEL_ID_BD_ANALOG;
        default:
            return DRUM_MODEL_ID_NONE;
    }
}

static __attribute__((noinline)) void brick6_render_synth_tracks(uint16_t entity_mask,
                                       uint32_t frames,
                                       uint8_t *out_drum_tracks)
{
    static float drum_fallback[AUDIO_BLOCK_SIZE];
    uint8_t drum_tracks = 0U;

    while (entity_mask != 0U)
    {
        const uint8_t track = (uint8_t)__builtin_ctz((unsigned int)entity_mask);
        entity_mask &= (uint16_t)(entity_mask - 1U);
        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
                ? &ctx_value : NULL;

        {
            const drum_model_id_t model_id = brick6_map_runtime_type_to_drum_model(ctx->type);
            if ((model_id == DRUM_MODEL_ID_COUNT) || (model_id == DRUM_MODEL_ID_NONE))
            {
                (void)drum_synth_set_model_for_instance(ctx->program_route.instance_id, DRUM_MODEL_ID_NONE);
            }
            else if (drum_synth_set_model_for_instance(ctx->program_route.instance_id, model_id) == 0U)
            {
                continue;
            }

            float *direct_mono = NULL;
            if (mixer_begin_external_mono_native(ctx->program_route.mix_track_id,
                                                 frames,
                                                 &direct_mono) != 0U)
            {
                drum_synth_process_block_for_instance(ctx->program_route.instance_id,
                                                      direct_mono,
                                                      frames);
                mixer_commit_external_mono_native(ctx->program_route.mix_track_id, frames);
            }
            else
            {
                drum_synth_process_block_for_instance(ctx->program_route.instance_id,
                                                      drum_fallback,
                                                      frames);
                mixer_submit_external_mono_native(ctx->program_route.mix_track_id,
                                                  drum_fallback,
                                                  frames);
            }
            drum_tracks++;
            continue;
        }
    }

    if (out_drum_tracks != NULL)
    {
        *out_drum_tracks = drum_tracks;
    }
}

static __attribute__((noinline)) void brick6_render_sampler_tracks(uint32_t frames, uint8_t *out_sampler_tracks)
{
    static float sampler_tmp_l[AUDIO_BLOCK_SIZE];
    static float sampler_tmp_r[AUDIO_BLOCK_SIZE];
    uint8_t sampler_tracks = 0U;

    uint32_t render_mask = brick6_sampler_runtime_render_track_mask();
    while (render_mask != 0U)
    {
        const uint8_t track = (uint8_t)__builtin_ctz(render_mask);
        render_mask &= render_mask - 1U;
        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
                ? &ctx_value : NULL;
        if ((ctx == NULL)
                || (ctx->program_route.active == 0U)
                || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
                || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U))
        {
            continue;
        }

        if ((brick6_sampler_runtime_track_has_active_ram_voice(ctx->program_route.entity_id) != 0U)
                && ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_STREAM)
                && ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_MULTI))
        {
            if (brick6_sampler_runtime_track_ram_is_mono(ctx->program_route.entity_id) != 0U)
            {
                float *direct_mono = NULL;
                if (mixer_begin_external_mono_native(ctx->program_route.mix_track_id,
                                                     frames,
                                                     &direct_mono) != 0U)
                {
                    memset(direct_mono, 0, frames * sizeof(float));
                    brick6_sampler_runtime_render_ram_track_mono(ctx,
                                                                  direct_mono,
                                                                  frames);
                    mixer_commit_external_mono_native(ctx->program_route.mix_track_id, frames);
                    sampler_tracks++;
                    continue;
                }
            }

            float *direct_l = NULL;
            float *direct_r = NULL;
            if (mixer_begin_external_stereo(ctx->program_route.mix_track_id, frames, &direct_l, &direct_r) != 0U)
            {
                memset(direct_l, 0, frames * sizeof(float));
                memset(direct_r, 0, frames * sizeof(float));
                brick6_sampler_runtime_render_ram_track(ctx, direct_l, direct_r, frames);
                mixer_commit_external_stereo(ctx->program_route.mix_track_id, frames);
                sampler_tracks++;
                continue;
            }
        }

        if ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_MULTI)
        {
            const uint8_t multi_mono_native =
                brick6_sampler_runtime_track_is_mono_native_ctx(ctx);
            if (multi_mono_native != 0U)
            {
                float *direct_mono = NULL;
                if (mixer_begin_external_multi_mono(ctx->program_route.mix_track_id,
                                                    frames,
                                                    &direct_mono) != 0U)
                {
                    memset(direct_mono, 0, frames * sizeof(float));
                    brick6_sampler_runtime_render_multi_track_mono(ctx,
                                                                    direct_mono,
                                                                    frames);
                    mixer_commit_external_multi_mono(ctx->program_route.mix_track_id, frames);
                }
            }
            else
            {
                float *direct_l = NULL;
                float *direct_r = NULL;
                if (mixer_begin_external_multi_stereo(ctx->program_route.mix_track_id,
                                                      frames,
                                                      &direct_l,
                                                      &direct_r) != 0U)
                {
                    memset(direct_l, 0, frames * sizeof(float));
                    memset(direct_r, 0, frames * sizeof(float));
                    brick6_sampler_runtime_render_multi_track(ctx,
                                                              direct_l,
                                                              direct_r,
                                                              frames);
                    mixer_commit_external_multi_stereo(ctx->program_route.mix_track_id, frames);
                }
            }
            sampler_tracks++;
            continue;
        }

        if ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_STREAM)
        {
            if (brick6_sampler_runtime_track_is_mono_native_ctx(ctx) != 0U)
            {
                float *direct_mono = NULL;
                if (mixer_begin_external_mono_native(ctx->program_route.mix_track_id,
                                                     frames,
                                                     &direct_mono) != 0U)
                {
                    memset(direct_mono, 0, frames * sizeof(float));
                    brick6_sampler_runtime_render_stream_track_mono(ctx,
                                                                    direct_mono,
                                                                    frames);
                    mixer_commit_external_mono_native(ctx->program_route.mix_track_id, frames);
                    sampler_tracks++;
                    continue;
                }
            }

            float *direct_l = NULL;
            float *direct_r = NULL;
            if (mixer_begin_external_stereo(ctx->program_route.mix_track_id,
                                            frames,
                                            &direct_l,
                                            &direct_r) != 0U)
            {
                memset(direct_l, 0, frames * sizeof(float));
                memset(direct_r, 0, frames * sizeof(float));
                brick6_sampler_runtime_render_stream_track(ctx,
                                                            direct_l,
                                                            direct_r,
                                                            frames);
                mixer_commit_external_stereo(ctx->program_route.mix_track_id, frames);
                sampler_tracks++;
                continue;
            }

            memset(sampler_tmp_l, 0, frames * sizeof(float));
            memset(sampler_tmp_r, 0, frames * sizeof(float));
            brick6_sampler_runtime_render_stream_track(ctx,
                                                        sampler_tmp_l,
                                                        sampler_tmp_r,
                                                        frames);
            mixer_submit_external_stereo(ctx->program_route.mix_track_id,
                                         sampler_tmp_l,
                                         sampler_tmp_r,
                                         frames);
            sampler_tracks++;
            continue;
        }

        memset(sampler_tmp_l, 0, frames * sizeof(float));
        memset(sampler_tmp_r, 0, frames * sizeof(float));
        brick6_sampler_runtime_render_track(ctx, sampler_tmp_l, sampler_tmp_r, frames);
        mixer_submit_external_stereo(ctx->program_route.mix_track_id, sampler_tmp_l, sampler_tmp_r, frames);
        sampler_tracks++;
    }

    if (out_sampler_tracks != NULL)
    {
        *out_sampler_tracks = sampler_tracks;
    }
}

static __attribute__((noinline)) void brick6_render_looper_tracks(uint32_t frames, uint8_t *out_looper_tracks)
{
    static float looper_tmp_l[AUDIO_BLOCK_SIZE];
    static float looper_tmp_r[AUDIO_BLOCK_SIZE];
    uint8_t looper_tracks = 0U;

    uint16_t playing_mask = brick6_looper_runtime_playing_mask();
    while (playing_mask != 0U)
    {
        const uint8_t track = (uint8_t)__builtin_ctz((unsigned)playing_mask);
        playing_mask &= (uint16_t)(playing_mask - 1U);
        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
                ? &ctx_value : NULL;
        if ((ctx == NULL)
                || (ctx->program_route.active == 0U)
                || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_LOOPER)
                || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
                || (brick6_looper_runtime_is_playing(track) == 0U))
        {
            continue;
        }

        float *direct_l = NULL;
        float *direct_r = NULL;
        if (mixer_begin_external_stereo(ctx->program_route.mix_track_id,
                                        frames,
                                        &direct_l,
                                        &direct_r) != 0U)
        {
            memset(direct_l, 0, frames * sizeof(float));
            memset(direct_r, 0, frames * sizeof(float));
            brick6_looper_runtime_render_track(ctx, direct_l, direct_r, frames);
            mixer_commit_external_stereo(ctx->program_route.mix_track_id, frames);
            looper_tracks++;
            continue;
        }

        memset(looper_tmp_l, 0, frames * sizeof(float));
        memset(looper_tmp_r, 0, frames * sizeof(float));
        brick6_looper_runtime_render_track(ctx, looper_tmp_l, looper_tmp_r, frames);
        mixer_submit_external_stereo(ctx->program_route.mix_track_id, looper_tmp_l, looper_tmp_r, frames);
        looper_tracks++;
    }

    if (out_looper_tracks != NULL)
    {
        *out_looper_tracks = looper_tracks;
    }
}

static __attribute__((noinline)) void brick6_render_prism_tracks(uint16_t entity_mask,
                                       uint32_t frames,
                                       uint8_t *out_prism_tracks)
{
    static float prism_tmp[AUDIO_BLOCK_SIZE];
    uint8_t prism_tracks = 0U;

    while (entity_mask != 0U)
    {
        const uint8_t track = (uint8_t)__builtin_ctz((unsigned int)entity_mask);
        entity_mask &= (uint16_t)(entity_mask - 1U);
        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
                ? &ctx_value : NULL;

        const uint8_t voice_count = synth_polyphony_get_render_voice_count(track);
        if (voice_count == 0U)
            continue;
        if (synth_waveform_audio_target_is(track, SYNTH_WAVEFORM_ENGINE_PRISM) != 0U)
        {
            uint8_t capture_instance = ctx->program_route.instance_id;
            if (voice_count > 1U)
            {
                const uint8_t voice = synth_polyphony_get_most_recent_renderable_voice(track);
                if (voice != SYNTH_POLYPHONY_NO_VOICE)
                    capture_instance = SYNTH_POLYPHONY_INSTANCE(track, voice);
            }
            synth_waveform_audio_select_instance(track, SYNTH_WAVEFORM_ENGINE_PRISM,
                                                 capture_instance);
        }
        if (voice_count > 1U)
        {
            const uint8_t poly_lfo_active = (mod_matrix_poly_route_mask(track) != 0U);
            uint8_t published = 0U;
            uint8_t renderable = synth_polyphony_get_renderable_voice_mask(track);
            if (renderable == 0U)
                continue;
            if (mixer_begin_external_poly(ctx->program_route.mix_track_id, frames) == 0U)
                continue;
            while (renderable != 0U)
            {
                const uint8_t voice = (uint8_t)__builtin_ctz((unsigned int)renderable);
                renderable &= (uint8_t)(renderable - 1U);
                const uint8_t instance = SYNTH_POLYPHONY_INSTANCE(track, voice);
                brick6_braids_runtime_sync_voice(ctx->program_route.instance_id, instance);
                if (poly_lfo_active != 0U)
                {
                    mixer_prepare_external_poly_voice(ctx->program_route.mix_track_id, track, voice);
                    mod_lfo_v1_process_poly_voice(track, instance, ctx, frames);
                }
                if (brick6_braids_runtime_render_instance(instance, prism_tmp, frames) == 0U)
                    memset(prism_tmp, 0, frames * sizeof(float));
                const uint8_t running = (poly_lfo_active != 0U)
                    ? mixer_process_external_poly_voice_prepared(
                        ctx->program_route.mix_track_id, track, voice, prism_tmp, frames,
                        synth_polyphony_get_voice_pan(track, voice))
                    : mixer_process_external_poly_voice(
                        ctx->program_route.mix_track_id, track, voice, prism_tmp, frames,
                        synth_polyphony_get_voice_pan(track, voice));
                published = 1U;
                if (running == 0U)
                    synth_polyphony_voice_release_complete(track, voice);
            }
            if (published != 0U)
            {
                mixer_commit_external_poly(ctx->program_route.mix_track_id, frames);
                prism_tracks++;
            }
            continue;
        }

        float *direct_mono = NULL;
        if (mixer_begin_external_mono_native(ctx->program_route.mix_track_id, frames, &direct_mono) != 0U)
        {
            if (brick6_braids_runtime_render_instance(ctx->program_route.instance_id, direct_mono, frames) != 0U)
            {
                mixer_commit_external_mono_native(ctx->program_route.mix_track_id, frames);
                prism_tracks++;
            }
            continue;
        }

        if (brick6_braids_runtime_render_instance(ctx->program_route.instance_id, prism_tmp, frames) != 0U)
        {
            mixer_submit_external_mono_native(ctx->program_route.mix_track_id, prism_tmp, frames);
            prism_tracks++;
        }
    }

    if (out_prism_tracks != NULL)
    {
        *out_prism_tracks = prism_tracks;
    }
}

static __attribute__((noinline)) void brick6_render_fm_tracks(uint16_t entity_mask,
                                    uint32_t frames,
                                    uint8_t *out_fm_tracks)
{
    static float fm_tmp[AUDIO_BLOCK_SIZE];
    uint8_t fm_tracks = 0U;

    while (entity_mask != 0U)
    {
        const uint8_t track = (uint8_t)__builtin_ctz((unsigned int)entity_mask);
        entity_mask &= (uint16_t)(entity_mask - 1U);
        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
                ? &ctx_value : NULL;

        const uint8_t voice_count = synth_polyphony_get_render_voice_count(track);
        if (voice_count == 0U)
            continue;
        if (voice_count > 1U)
        {
            const uint8_t renderable = synth_polyphony_get_renderable_voice_mask(track);
            if (renderable == 0U)
                continue;
            if (mixer_begin_external_poly(ctx->program_route.mix_track_id, frames) == 0U)
                continue;
            uint8_t voices_published = 0U;
            uint8_t pending = renderable;
            while (pending != 0U)
            {
                const uint8_t voice = (uint8_t)__builtin_ctz((unsigned int)pending);
                pending &= (uint8_t)(pending - 1U);
                const uint8_t instance = SYNTH_POLYPHONY_INSTANCE(track, voice);
                brick6_fm_runtime_sync_voice_if_needed(ctx->program_route.instance_id, instance);
                (void)brick6_fm_runtime_render_instance(instance, fm_tmp, frames);
                const uint8_t running = mixer_process_external_poly_voice(
                    ctx->program_route.mix_track_id, track, voice, fm_tmp, frames,
                    synth_polyphony_get_voice_pan(track, voice));
                voices_published = 1U;
                if (running == 0U)
                    synth_polyphony_voice_release_complete(track, voice);
            }
            if (voices_published != 0U)
            {
                mixer_commit_external_poly(ctx->program_route.mix_track_id, frames);
                fm_tracks++;
            }
            continue;
        }

        const uint8_t instance = ctx->program_route.instance_id;
        float *direct_mono = NULL;
        if (mixer_begin_external_mono_native(ctx->program_route.mix_track_id, frames, &direct_mono) != 0U)
        {
            if (brick6_fm_runtime_render_instance(instance, direct_mono, frames) != 0U)
            {
                mixer_commit_external_mono_native(ctx->program_route.mix_track_id, frames);
                fm_tracks++;
            }
            else
                synth_polyphony_voice_release_complete(track, 0U);
            continue;
        }

        if (brick6_fm_runtime_render_instance(instance, fm_tmp, frames) != 0U)
        {
            mixer_submit_external_mono_native(ctx->program_route.mix_track_id, fm_tmp, frames);
            fm_tracks++;
        }
        else
            synth_polyphony_voice_release_complete(track, 0U);
    }

    if (out_fm_tracks != NULL)
        *out_fm_tracks = fm_tracks;
}

static __attribute__((noinline)) void brick6_render_wave_tracks(uint16_t entity_mask,
                                      uint32_t frames,
                                      uint8_t *out_wave_tracks)
{
    static float wave_tmp[AUDIO_BLOCK_SIZE];
    uint8_t wave_tracks = 0U;

    while (entity_mask != 0U)
    {
        const uint8_t track = (uint8_t)__builtin_ctz((unsigned int)entity_mask);
        entity_mask &= (uint16_t)(entity_mask - 1U);
        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
                ? &ctx_value : NULL;

        const uint8_t voice_count = synth_polyphony_get_render_voice_count(track);
        if (voice_count == 0U)
            continue;
        if (synth_waveform_audio_target_is(track, SYNTH_WAVEFORM_ENGINE_WAVE) != 0U)
        {
            uint8_t capture_instance = ctx->program_route.instance_id;
            if (voice_count > 1U)
            {
                const uint8_t voice = synth_polyphony_get_most_recent_renderable_voice(track);
                if (voice != SYNTH_POLYPHONY_NO_VOICE)
                    capture_instance = SYNTH_POLYPHONY_INSTANCE(track, voice);
            }
            synth_waveform_audio_select_instance(track, SYNTH_WAVEFORM_ENGINE_WAVE,
                                                 capture_instance);
        }
        if (voice_count > 1U)
        {
            const uint8_t poly_lfo_active = (mod_matrix_poly_route_mask(track) != 0U);
            uint8_t published = 0U;
            uint8_t renderable = synth_polyphony_get_renderable_voice_mask(track);
            if (renderable == 0U)
                continue;
            if (mixer_begin_external_poly(ctx->program_route.mix_track_id, frames) == 0U)
                continue;
            while (renderable != 0U)
            {
                const uint8_t voice = (uint8_t)__builtin_ctz((unsigned int)renderable);
                renderable &= (uint8_t)(renderable - 1U);
                const uint8_t instance = SYNTH_POLYPHONY_INSTANCE(track, voice);
                brick6_wave_runtime_sync_voice(ctx->program_route.instance_id, instance);
                if (poly_lfo_active != 0U)
                {
                    mixer_prepare_external_poly_voice(ctx->program_route.mix_track_id, track, voice);
                    mod_lfo_v1_process_poly_voice(track, instance, ctx, frames);
                }
                if ((brick6_wave_runtime_prepare_block(instance, frames, 1U) == 0U)
                        || (brick6_wave_runtime_render_instance(instance, wave_tmp, frames) == 0U))
                    memset(wave_tmp, 0, frames * sizeof(float));
                const uint8_t running = (poly_lfo_active != 0U)
                    ? mixer_process_external_poly_voice_prepared(
                        ctx->program_route.mix_track_id, track, voice, wave_tmp, frames,
                        synth_polyphony_get_voice_pan(track, voice))
                    : mixer_process_external_poly_voice(
                        ctx->program_route.mix_track_id, track, voice, wave_tmp, frames,
                        synth_polyphony_get_voice_pan(track, voice));
                published = 1U;
                if (running == 0U)
                    synth_polyphony_voice_release_complete(track, voice);
            }
            if (published != 0U)
            {
                mixer_commit_external_poly(ctx->program_route.mix_track_id, frames);
                wave_tracks++;
            }
            continue;
        }

        if (brick6_wave_runtime_prepare_block(
                ctx->program_route.instance_id,
                frames,
                mixer_track_vca_requires_source(ctx->program_route.mix_track_id)) == 0U)
        {
            continue;
        }

        float *direct_mono = NULL;
        if (mixer_begin_external_mono_native(ctx->program_route.mix_track_id, frames, &direct_mono) != 0U)
        {
            if (brick6_wave_runtime_render_instance(ctx->program_route.instance_id, direct_mono, frames) != 0U)
            {
                mixer_commit_external_mono_native(ctx->program_route.mix_track_id, frames);
                wave_tracks++;
            }
            continue;
        }

        if (brick6_wave_runtime_render_instance(ctx->program_route.instance_id, wave_tmp, frames) != 0U)
        {
            mixer_submit_external_mono_native(ctx->program_route.mix_track_id, wave_tmp, frames);
            wave_tracks++;
        }
    }

    if (out_wave_tracks != NULL)
    {
        *out_wave_tracks = wave_tracks;
    }
}
static __attribute__((noinline)) void brick6_render_stack_tracks(uint16_t entity_mask,
                                       uint32_t frames,
                                       uint8_t *out_stack_tracks)
{
    static float stack_tmp[AUDIO_BLOCK_SIZE];
    uint8_t stack_tracks = 0U;

    while (entity_mask != 0U)
    {
        const uint8_t track = (uint8_t)__builtin_ctz((unsigned int)entity_mask);
        entity_mask &= (uint16_t)(entity_mask - 1U);
        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
                ? &ctx_value : NULL;

        const uint8_t voice_count = synth_polyphony_get_render_voice_count(track);
        if (voice_count == 0U)
            continue;
        if (voice_count > 1U)
        {
            const uint8_t poly_lfo_active = (mod_matrix_poly_route_mask(track) != 0U);
            uint8_t published = 0U;
            uint8_t renderable = synth_polyphony_get_renderable_voice_mask(track);
            if (renderable == 0U)
                continue;
            if (mixer_begin_external_poly(ctx->program_route.mix_track_id, frames) == 0U)
                continue;
            while (renderable != 0U)
            {
                const uint8_t voice = (uint8_t)__builtin_ctz((unsigned int)renderable);
                renderable &= (uint8_t)(renderable - 1U);
                const uint8_t instance = SYNTH_POLYPHONY_INSTANCE(track, voice);
                brick6_stack_runtime_sync_voice(ctx->program_route.instance_id, instance);
                if (poly_lfo_active != 0U)
                {
                    mixer_prepare_external_poly_voice(ctx->program_route.mix_track_id, track, voice);
                    mod_lfo_v1_process_poly_voice(track, instance, ctx, frames);
                }
                if (brick6_stack_runtime_render_instance(instance, stack_tmp, frames, 1U) == 0U)
                    memset(stack_tmp, 0, frames * sizeof(float));
                const uint8_t running = (poly_lfo_active != 0U)
                    ? mixer_process_external_poly_voice_prepared(
                        ctx->program_route.mix_track_id, track, voice, stack_tmp, frames,
                        synth_polyphony_get_voice_pan(track, voice))
                    : mixer_process_external_poly_voice(
                        ctx->program_route.mix_track_id, track, voice, stack_tmp, frames,
                        synth_polyphony_get_voice_pan(track, voice));
                published = 1U;
                if (running == 0U)
                    synth_polyphony_voice_release_complete(track, voice);
            }
            if (published != 0U)
            {
                mixer_commit_external_poly(ctx->program_route.mix_track_id, frames);
                stack_tracks++;
            }
            continue;
        }

        const uint8_t downstream_source_required =
            mixer_track_vca_requires_source(ctx->program_route.mix_track_id);
        float *direct_mono = NULL;
        if (mixer_begin_external_mono_native(ctx->program_route.mix_track_id, frames, &direct_mono) != 0U)
        {
            if (brick6_stack_runtime_render_instance(
                    ctx->program_route.instance_id,
                    direct_mono,
                    frames,
                    downstream_source_required) != 0U)
            {
                mixer_commit_external_mono_native(ctx->program_route.mix_track_id, frames);
                stack_tracks++;
            }
            continue;
        }

        if (brick6_stack_runtime_render_instance(
                ctx->program_route.instance_id,
                stack_tmp,
                frames,
                downstream_source_required) != 0U)
        {
            mixer_submit_external_mono_native(ctx->program_route.mix_track_id, stack_tmp, frames);
            stack_tracks++;
        }
    }

    if (out_stack_tracks != NULL)
    {
        *out_stack_tracks = stack_tracks;
    }
}

void brick6_audio_runtime_init(void)
{
    g_runtime_track_enabled = 1U;
    synth_polyphony_init();
    metronome_runtime_init();
}

ITCM_TEXT void brick6_audio_runtime_dsp(StereoTrack *tracks,
                              uint32_t track_count,
                              uint32_t frames)
{
    brick6_sampler_runtime_audio_maintenance();
    brick6_looper_runtime_audio_maintenance();
    brick6_publish_owned_external_sources(frames);
    const uint16_t drum_entity_mask = audio_note_engine_adapter_entity_mask(
        TRACK_RUNTIME_ENGINE_DRUM);
    const uint8_t synth_runtime_enabled = (uint8_t)(
        drum_entity_mask != 0U);

    if (((synth_runtime_enabled == 0U) && (g_runtime_track_enabled != 0U))
            || ((synth_runtime_enabled != 0U) && (g_runtime_track_enabled == 0U)))
    {
        drum_synth_all_notes_off_all();
    }
    g_runtime_track_enabled = synth_runtime_enabled;

    mod_lfo_v1_process_block(frames);

    if (synth_runtime_enabled != 0U)
    {
        uint8_t drum_processed = 0U;
        brick6_render_synth_tracks(drum_entity_mask, frames, &drum_processed);

        if (drum_processed != g_runtime_last_drum_processed)
        {
            g_runtime_last_drum_processed = drum_processed;
        }
    }

    if (brick6_sampler_runtime_render_track_mask() != 0U)
    {
        uint8_t sampler_tracks = 0U;
        brick6_render_sampler_tracks(frames, &sampler_tracks);
        (void)sampler_tracks;
    }

    if (brick6_looper_runtime_playing_mask() != 0U)
    {
        uint8_t looper_tracks = 0U;
        brick6_render_looper_tracks(frames, &looper_tracks);
        (void)looper_tracks;
    }

    const uint16_t prism_entity_mask = audio_note_engine_adapter_entity_mask(
        TRACK_RUNTIME_ENGINE_PRISM);
    if (prism_entity_mask != 0U)
    {
        uint8_t prism_tracks = 0U;
        brick6_render_prism_tracks(prism_entity_mask, frames, &prism_tracks);
        (void)prism_tracks;
    }

    const uint16_t stack_entity_mask = audio_note_engine_adapter_entity_mask(
        TRACK_RUNTIME_ENGINE_STACK);
    if (stack_entity_mask != 0U)
    {
        uint8_t stack_tracks = 0U;
        brick6_render_stack_tracks(stack_entity_mask, frames, &stack_tracks);
        (void)stack_tracks;
    }

    synth_waveform_audio_begin_block(frames);
    const uint16_t wave_entity_mask = audio_note_engine_adapter_entity_mask(
        TRACK_RUNTIME_ENGINE_WAVE);
    if (wave_entity_mask != 0U)
    {
        uint8_t wave_tracks = 0U;
        brick6_render_wave_tracks(wave_entity_mask, frames, &wave_tracks);
        (void)wave_tracks;
    }

    const uint16_t fm_entity_mask = audio_note_engine_adapter_entity_mask(
        TRACK_RUNTIME_ENGINE_FM);
    if (fm_entity_mask != 0U)
    {
        uint8_t fm_tracks = 0U;
        brick6_render_fm_tracks(fm_entity_mask, frames, &fm_tracks);
        (void)fm_tracks;
    }

    mixer_process(tracks, track_count, frames);

    if (track_count > 0U)
    {
        (void)sd_preview_render_main(tracks[0].L, tracks[0].R, frames);
    }

}
