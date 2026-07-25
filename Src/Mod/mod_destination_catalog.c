#include "Mod/mod_destination_catalog.h"

#include "Audio/audio_xfade.h"
#include "Audio/drum_synth.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Param/param_filter.h"
#include "Param/param_registry.h"
#include "Param/param_registry_backends.h"
#include "Param/param_wave_labels.h"
#include "Seq/seq_types.h"
#include "mixer.h"

typedef struct
{
    uint8_t valid;
    uint8_t ui_family;
    uint8_t ui_type;
    uint8_t rt_bind_state;
    uint8_t rt_family;
    uint8_t rt_type;
    uint8_t rt_mix_track_id;
    uint16_t count;
    param_id_t index_to_param[PARAM_COUNT + 1U];
    uint16_t param_to_index[PARAM_COUNT];
} mod_destination_cache_t;

typedef struct
{
    uint8_t valid;
    uint8_t value;
} mod_destination_midi_cc_cache_t;

static mod_destination_cache_t g_mod_destination_cache[SEQ_TRACK_COUNT];
static mod_destination_midi_cc_cache_t g_mod_destination_midi_cc_cache[SEQ_TRACK_COUNT][12U];

static float mod_destination_clampf(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

static uint8_t mod_destination_is_simple_mix(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_MIX_LEVEL:
        case PARAM_MIX_PAN:
        case PARAM_MIX_SEND1:
        case PARAM_MIX_SEND2:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_direct_filter(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_FILTER_CUTOFF:
        case PARAM_FILTER_RESONANCE:
        case PARAM_FILTER_EG_AMT:
        case PARAM_FILTER_ATTACK:
        case PARAM_FILTER_DECAY:
        case PARAM_FILTER_SUSTAIN:
        case PARAM_FILTER_RELEASE:
        case PARAM_FILTER_EQ_LOW:
        case PARAM_FILTER_EQ_MID:
        case PARAM_FILTER_EQ_HIGH:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_direct_vca(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_VCA_ATTACK:
        case PARAM_VCA_DECAY:
        case PARAM_VCA_SUSTAIN:
        case PARAM_VCA_RELEASE:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_direct_sampler(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_SAMPLER_GAIN:
        case PARAM_SAMPLER_START:
        case PARAM_SAMPLER_END:
        case PARAM_SAMPLER_LOOP_START:
        case PARAM_SAMPLER_MODE:
        case PARAM_SAMPLER_TUNE:
        case PARAM_SAMPLER_SLICE_COUNT:
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
        case PARAM_SAMPLER_CLIP_PITCH:
        case PARAM_SAMPLER_CLIP_PLAY_MODE:
        case PARAM_SAMPLER_CLIP_LOOP:
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
        case PARAM_SAMPLER_CLIP_GRAIN:
        case PARAM_SAMPLER_CLIP_HOP:
        case PARAM_SAMPLER_MULTI_LOOP:
        case PARAM_LOOPER_XFADE:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_direct_wave(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_WAVE_EDIT:
        case PARAM_WAVE_FINE:
        case PARAM_WAVE_COARSE:
        case PARAM_WAVE_FM:
        case PARAM_WAVE_TIMBRE:
        case PARAM_WAVE_MODULATION:
        case PARAM_WAVE_COLOR:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_direct_drum(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_DRUM_TRX_BD_PITCH:
        case PARAM_DRUM_TRX_BD_DECAY:
        case PARAM_DRUM_TRX_BD_HARMONICS:
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_direct_midi_cc(param_id_t dest)
{
    return param_backend_is_midi_cc_id(dest);
}

static uint8_t mod_destination_midi_cc_cache_index(param_id_t dest, uint8_t *out_index)
{
    if ((out_index == NULL) || (dest < PARAM_MIDI_CC1_1) || (dest > PARAM_MIDI_CC3_4))
    {
        return 0U;
    }

    *out_index = (uint8_t)(dest - PARAM_MIDI_CC1_1);
    return (*out_index < 12U) ? 1U : 0U;
}

static uint8_t mod_destination_apply_simple_mix_rt(uint8_t track,
                                                   param_id_t dest,
                                                   const track_runtime_ctx_t *ctx,
                                                   float value)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MASTER)
            || (ctx->mix_track_id >= MIXER_MAX_TRACKS))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_MIX_LEVEL:
            mixer_set_track_gain(ctx->mix_track_id, mod_destination_clampf(value, 0.0f, 2.0f));
            return 1U;
        case PARAM_MIX_PAN:
            mixer_set_track_pan(ctx->mix_track_id, mod_destination_clampf(value, -1.0f, 1.0f));
            return 1U;
        case PARAM_MIX_SEND1:
            mixer_set_track_send_level(ctx->mix_track_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_MIX_SEND2:
            mixer_set_track_send_level(ctx->mix_track_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_filter_rt(uint8_t track,
                                               param_id_t dest,
                                               const track_runtime_ctx_t *ctx,
                                               float value)
{
    (void)track;

    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->mix_track_id >= MIXER_MAX_TRACKS)
            || ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
                && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_FILTER_CUTOFF:
            mixer_set_track_filter_cutoff(ctx->mix_track_id, param_filter_ui127_to_cutoff_hz(value));
            return 1U;
        case PARAM_FILTER_RESONANCE:
            mixer_set_track_filter_resonance(ctx->mix_track_id, param_filter_ui127_to_resonance(value));
            return 1U;
        case PARAM_FILTER_EG_AMT:
            mixer_set_track_filter_eg_amount(ctx->mix_track_id, param_filter_ui127_to_eg_amount(value));
            return 1U;
        case PARAM_FILTER_ATTACK:
            mixer_set_track_filter_attack(ctx->mix_track_id, param_filter_ui127_to_attack_s(value));
            return 1U;
        case PARAM_FILTER_DECAY:
            mixer_set_track_filter_decay(ctx->mix_track_id, param_filter_ui127_to_decay_s(value));
            return 1U;
        case PARAM_FILTER_SUSTAIN:
            mixer_set_track_filter_sustain(ctx->mix_track_id, param_filter_ui127_to_sustain(value));
            return 1U;
        case PARAM_FILTER_RELEASE:
            mixer_set_track_filter_release(ctx->mix_track_id, param_filter_ui127_to_release_s(value));
            return 1U;
        case PARAM_FILTER_KEYTRK:
            mixer_set_track_filter_keytrack(ctx->mix_track_id, param_filter_ui127_to_keytrack(value));
            return 1U;
        case PARAM_FILTER_EQ_LOW:
            mixer_set_track_filter_eq_low(ctx->mix_track_id, param_filter_eq_ui127_to_db(value));
            return 1U;
        case PARAM_FILTER_EQ_MID:
            mixer_set_track_filter_eq_mid(ctx->mix_track_id, param_filter_eq_ui127_to_db(value));
            return 1U;
        case PARAM_FILTER_EQ_HIGH:
            mixer_set_track_filter_eq_high(ctx->mix_track_id, param_filter_eq_ui127_to_db(value));
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_vca_rt(uint8_t track,
                                            param_id_t dest,
                                            const track_runtime_ctx_t *ctx,
                                            float value)
{
    (void)track;

    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->mix_track_id >= MIXER_MAX_TRACKS)
            || (track_runtime_supports_vca_gate(ctx) == 0U))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_VCA_ATTACK:
            mixer_set_track_vca_attack(ctx->mix_track_id, param_filter_ui127_to_attack_s(value));
            return 1U;
        case PARAM_VCA_DECAY:
            mixer_set_track_vca_decay(ctx->mix_track_id, param_filter_ui127_to_decay_s(value));
            return 1U;
        case PARAM_VCA_SUSTAIN:
            mixer_set_track_vca_sustain(ctx->mix_track_id, param_filter_ui127_to_sustain(value));
            return 1U;
        case PARAM_VCA_RELEASE:
        {
            const float release_s = param_filter_ui127_to_release_s(value);
            mixer_set_track_vca_release(ctx->mix_track_id, release_s);
            if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            {
                brick6_braids_runtime_set_vca_release_seconds(ctx->instance_id, release_s);
            }
            return 1U;
        }
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_sampler_rt(uint8_t track,
                                                param_id_t dest,
                                                const track_runtime_ctx_t *ctx,
                                                float value)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_SAMPLER_GAIN:
            if (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI)
            {
                brick6_sampler_runtime_set_multi_gain(track, mod_destination_clampf(value, 0.0f, 2.0f));
            }
            else
            {
                brick6_sampler_runtime_set_gain(track, mod_destination_clampf(value, 0.0f, 2.0f));
            }
            return 1U;
        case PARAM_SAMPLER_START:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER) { return 0U; }
            brick6_sampler_runtime_set_start(track, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_END:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER) { return 0U; }
            brick6_sampler_runtime_set_end(track, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_LOOP_START:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER) { return 0U; }
            brick6_sampler_runtime_set_loop_start(track, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_MODE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER) { return 0U; }
            brick6_sampler_runtime_set_mode(track, (uint8_t)(mod_destination_clampf(value, 0.0f, 3.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_TUNE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER) { return 0U; }
            brick6_sampler_runtime_set_tune(track, mod_destination_clampf(value, -24.0f, 24.0f));
            return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
        {
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER) { return 0U; }
            static const uint8_t counts[] = {0U, 2U, 4U, 8U, 16U, 32U, 64U};
            const uint8_t idx = (uint8_t)(mod_destination_clampf(value, 0.0f, 6.0f) + 0.5f);
            brick6_sampler_runtime_set_slice_count(track, counts[idx]);
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP) { return 0U; }
            brick6_sampler_runtime_set_clip_source_bpm(track, mod_destination_clampf(value, 40.0f, 300.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP) { return 0U; }
            brick6_sampler_runtime_set_clip_sync_length(track, (uint8_t)(mod_destination_clampf(value, 0.0f, 4.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_PITCH:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP) { return 0U; }
            brick6_sampler_runtime_set_clip_pitch(track, mod_destination_clampf(value, -12.0f, 12.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_PLAY_MODE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP) { return 0U; }
            brick6_sampler_runtime_set_clip_play_mode(track, (uint8_t)(mod_destination_clampf(value, 0.0f, 1.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_LOOP:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP) { return 0U; }
            brick6_sampler_runtime_set_clip_loop(track, (uint8_t)(mod_destination_clampf(value, 0.0f, 1.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP) { return 0U; }
            brick6_sampler_runtime_set_clip_stretch_mode(track, (uint8_t)(mod_destination_clampf(value, 0.0f, 2.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_GRAIN:
        {
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP) { return 0U; }
            static const uint16_t grain_frames[] = {384U, 512U, 768U, 1024U, 1536U, 2048U};
            const uint8_t idx = (uint8_t)(mod_destination_clampf(value, 0.0f, 5.0f) + 0.5f);
            brick6_sampler_runtime_set_clip_grain_size(track, grain_frames[idx]);
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_HOP:
        case PARAM_SAMPLER_CLIP_SEARCH:
            return (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_CLIP) ? 1U : 0U;
        case PARAM_SAMPLER_MULTI_LOOP:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_MULTI) { return 0U; }
            brick6_sampler_runtime_set_multi_loop(track, (mod_destination_clampf(value, 0.0f, 1.0f) >= 0.5f) ? 1U : 0U);
            return 1U;
        case PARAM_LOOPER_XFADE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_LOOPER) { return 0U; }
            audio_xfade_set(mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_wave_rt(uint8_t track,
                                             param_id_t dest,
                                             const track_runtime_ctx_t *ctx,
                                             float value)
{
    (void)track;

    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_WAVE_EDIT:
            brick6_braids_runtime_set_edit(ctx->instance_id,
                                           (float)(uint8_t)(mod_destination_clampf(value, 0.0f, 38.0f) + 0.5f));
            return 1U;
        case PARAM_WAVE_FINE:
            brick6_braids_runtime_set_fine(ctx->instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_COARSE:
            brick6_braids_runtime_set_coarse(ctx->instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_FM:
            brick6_braids_runtime_set_fm(ctx->instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_TIMBRE:
            brick6_braids_runtime_set_timbre(ctx->instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_MODULATION:
            brick6_braids_runtime_set_modulation(ctx->instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_COLOR:
            brick6_braids_runtime_set_color(ctx->instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_drum_rt(uint8_t track,
                                             param_id_t dest,
                                             const track_runtime_ctx_t *ctx,
                                             float value)
{
    (void)track;

    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
            || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_DRUM_TRX_BD_PITCH:
            return drum_synth_set_param_for_instance(ctx->instance_id,
                                                     dest,
                                                     mod_destination_clampf(value, -48.0f, 24.0f));
        case PARAM_DRUM_TRX_BD_DECAY:
            return drum_synth_set_param_for_instance(ctx->instance_id,
                                                     dest,
                                                     mod_destination_clampf(value, 0.01f, 2.0f));
        case PARAM_DRUM_TRX_BD_HARMONICS:
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
            return drum_synth_set_param_for_instance(ctx->instance_id,
                                                     dest,
                                                     mod_destination_clampf(value, 0.0f, 1.0f));
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_midi_cc_rt(uint8_t track,
                                                param_id_t dest,
                                                const track_runtime_ctx_t *ctx,
                                                float value)
{
    const uint8_t midi_track = ((ctx != NULL) && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI)) ? 1U : 0U;
    const uint8_t hybrid_input_track =
        ((ctx != NULL)
         && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
         && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_HYBRID)) ? 1U : 0U;

    if ((track >= SEQ_TRACK_COUNT)
            || (ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || ((midi_track == 0U) && (hybrid_input_track == 0U))
            || (mod_destination_is_direct_midi_cc(dest) == 0U))
    {
        return 0U;
    }

    uint8_t cache_index = 0U;
    if (mod_destination_midi_cc_cache_index(dest, &cache_index) == 0U)
    {
        return 0U;
    }

    const uint8_t cc_value = (uint8_t)(mod_destination_clampf(value, 0.0f, 127.0f) + 0.5f);
    mod_destination_midi_cc_cache_t *const cache = &g_mod_destination_midi_cc_cache[track][cache_index];
    if ((cache->valid != 0U) && (cache->value == cc_value))
    {
        return 1U;
    }

    if (param_backend_send_midi_cc(track, dest, (float)cc_value) == 0U)
    {
        return 0U;
    }

    cache->valid = 1U;
    cache->value = cc_value;
    return 1U;
}

uint8_t mod_destination_catalog_apply_rt(uint8_t track,
                                         param_id_t dest,
                                         const track_runtime_ctx_t *ctx,
                                         float value)
{
    if (mod_destination_is_simple_mix(dest) != 0U)
    {
        return mod_destination_apply_simple_mix_rt(track, dest, ctx, value);
    }
    if (mod_destination_is_direct_filter(dest) != 0U)
    {
        return mod_destination_apply_filter_rt(track, dest, ctx, value);
    }
    if (mod_destination_is_direct_vca(dest) != 0U)
    {
        return mod_destination_apply_vca_rt(track, dest, ctx, value);
    }
    if (mod_destination_is_direct_sampler(dest) != 0U)
    {
        return mod_destination_apply_sampler_rt(track, dest, ctx, value);
    }
    if (mod_destination_is_direct_wave(dest) != 0U)
    {
        return mod_destination_apply_wave_rt(track, dest, ctx, value);
    }
    if (mod_destination_is_direct_drum(dest) != 0U)
    {
        return mod_destination_apply_drum_rt(track, dest, ctx, value);
    }
    if (mod_destination_is_direct_midi_cc(dest) != 0U)
    {
        return mod_destination_apply_midi_cc_rt(track, dest, ctx, value);
    }

    return param_registry_apply_track_value_rt_fast(dest, track, value);
}

static uint8_t mod_destination_is_internal_lfo_param(param_id_t id)
{
    switch (id)
    {
        case PARAM_LFO1_RATE:
        case PARAM_LFO1_SHAPE:
        case PARAM_LFO1_DELAY:
        case PARAM_LFO1_TRIG:
        case PARAM_LFO1_FADE:
        case PARAM_LFO1_PHASE_SLEW:
        case PARAM_LFO2_RATE:
        case PARAM_LFO2_SHAPE:
        case PARAM_LFO2_DELAY:
        case PARAM_LFO2_TRIG:
        case PARAM_LFO2_FADE:
        case PARAM_LFO2_PHASE_SLEW:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_param_matches_track_context(ui_track_family_t family,
                                                           param_id_t dest,
                                                           track_runtime_param_domain_t domain,
                                                           const track_runtime_ctx_t *ctx)
{
    if (domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
    {
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
        {
            return 0U;
        }
        if ((dest == PARAM_LOOPER_ARM)
                || (dest == PARAM_LOOPER_LEN)
                || (dest == PARAM_LOOPER_PLAY)
                || (dest == PARAM_LOOPER_STRETCH)
                || (dest == PARAM_LOOPER_PITCH)
                || (dest == PARAM_LOOPER_GRAIN)
                || (dest == PARAM_SAMPLER_SAMPLE)
                || (dest == PARAM_SAMPLER_CLIP_SEARCH)
                || ((dest >= PARAM_MASTER_FX1_TYPE) && (dest <= PARAM_MASTER_FX4_B))
                || (((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_DRUM_TRX_BD)
                    && (dest >= PARAM_DRUM_TRX_BD_PITCH)
                    && (dest <= PARAM_DRUM_TRX_BD_DRIVE)))
        {
            return 0U;
        }
        if ((dest == PARAM_WAVE_PHASE_RESET) || (dest == PARAM_MIDI_PROGRAM))
        {
            return 0U;
        }

        uint8_t tone_slot = 0U;
        if (track_runtime_tone_param_to_slot((track_runtime_type_t)ctx->type, dest, &tone_slot) == 0U)
        {
            return 0U;
        }
        (void)tone_slot;
        return 1U;
    }

    if (domain == TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
    {
        if (family == UI_TRACK_FAMILY_MIDI)
        {
            return 0U;
        }
        if ((dest == PARAM_FILTER_TYPE)
                || (dest == PARAM_FILTER_ENVRST)
                || (dest == PARAM_FILTER_ENVDLY)
                || (dest == PARAM_FILTER_KEYTRK))
        {
            return 0U;
        }

        return (((dest >= PARAM_FILTER_TYPE) && (dest <= PARAM_FILTER_ENVDLY))
                || ((dest >= PARAM_FILTER_EQ_LOW) && (dest <= PARAM_FILTER_EQ_HIGH))) ? 1U : 0U;
    }

    if (domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
    {
        if ((family == UI_TRACK_FAMILY_MASTER) || (ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
        {
            return 0U;
        }

        if (mod_destination_is_direct_vca(dest) != 0U)
        {
            return track_runtime_supports_vca_gate(ctx);
        }

        return ((dest == PARAM_MIX_LEVEL)
                || (dest == PARAM_MIX_PAN)
                || (dest == PARAM_MIX_SEND1)
                || (dest == PARAM_MIX_SEND2)) ? 1U : 0U;
    }

    return 0U;
}

static track_runtime_param_status_t mod_destination_effective_status_from_ctx(const track_runtime_ctx_t *ctx,
                                                                              track_runtime_resource_t resource)
{
    if (ctx == NULL)
    {
        return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }

    switch (resource)
    {
        case TRACK_RUNTIME_RESOURCE_NONE:
            return TRACK_RUNTIME_PARAM_ALLOWED;
        case TRACK_RUNTIME_RESOURCE_FILTER:
            if ((ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_OFF))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return TRACK_RUNTIME_PARAM_ALLOWED;
        case TRACK_RUNTIME_RESOURCE_SYNTH:
            if ((ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                    || ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                        && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return TRACK_RUNTIME_PARAM_ALLOWED;
        case TRACK_RUNTIME_RESOURCE_PLAY:
            return ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI))
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
        case TRACK_RUNTIME_RESOURCE_MIX:
            if ((ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MASTER)
                    || (ctx->mix_track_id >= SEQ_TRACK_COUNT))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return TRACK_RUNTIME_PARAM_ALLOWED;
        default:
            return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }
}

uint8_t mod_destination_catalog_supported_fast(uint8_t track,
                                               param_id_t dest,
                                               ui_track_family_t family,
                                               ui_track_type_t type,
                                               const track_runtime_ctx_t *ctx)
{
    (void)type;

    if ((track >= SEQ_TRACK_COUNT) || (dest >= PARAM_COUNT) || (mod_destination_is_internal_lfo_param(dest) != 0U))
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(dest);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX))
    {
        return 0U;
    }

    if (mod_destination_param_matches_track_context(family, dest, rule.domain, ctx) == 0U)
    {
        return 0U;
    }

    const track_runtime_param_status_t status = mod_destination_effective_status_from_ctx(ctx, rule.resource);
    return ((status == TRACK_RUNTIME_PARAM_ALLOWED) || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

static uint8_t mod_destination_cache_matches_context(const mod_destination_cache_t *cache,
                                                     ui_track_family_t family,
                                                     ui_track_type_t type,
                                                     const track_runtime_ctx_t *ctx)
{
    if ((cache == NULL) || (cache->valid == 0U))
    {
        return 0U;
    }

    const uint8_t ctx_bind_state = (ctx != NULL) ? ctx->bind_state : 0xFFU;
    const uint8_t ctx_family = (ctx != NULL) ? ctx->family : 0xFFU;
    const uint8_t ctx_type = (ctx != NULL) ? ctx->type : 0xFFU;
    const uint8_t ctx_mix_track_id = (ctx != NULL) ? ctx->mix_track_id : 0xFFU;

    return ((cache->ui_family == (uint8_t)family)
            && (cache->ui_type == (uint8_t)type)
            && (cache->rt_bind_state == ctx_bind_state)
            && (cache->rt_family == ctx_family)
            && (cache->rt_type == ctx_type)
            && (cache->rt_mix_track_id == ctx_mix_track_id)) ? 1U : 0U;
}

static mod_destination_cache_t *mod_destination_cache_resolve(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }

    track_runtime_refresh_track(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    const ui_track_family_t family = ui_get_track_family(track);
    const ui_track_type_t type = ui_get_track_type(track);
    mod_destination_cache_t *const cache = &g_mod_destination_cache[track];

    if (mod_destination_cache_matches_context(cache, family, type, ctx) != 0U)
    {
        return cache;
    }

    cache->index_to_param[0] = MOD_DESTINATION_NONE;
    for (uint16_t raw = 0U; raw < (uint16_t)PARAM_COUNT; ++raw)
    {
        cache->param_to_index[raw] = 0U;
    }

    uint16_t count = 1U;
    for (uint16_t raw = 0U; raw < (uint16_t)PARAM_COUNT; ++raw)
    {
        const param_id_t param = (param_id_t)raw;
        if (mod_destination_catalog_supported_fast(track, param, family, type, ctx) == 0U)
        {
            continue;
        }

        if (count <= (uint16_t)PARAM_COUNT)
        {
            cache->index_to_param[count] = param;
            cache->param_to_index[raw] = count;
        }
        ++count;
    }

    cache->count = count;
    cache->ui_family = (uint8_t)family;
    cache->ui_type = (uint8_t)type;
    cache->rt_bind_state = (ctx != NULL) ? ctx->bind_state : 0xFFU;
    cache->rt_family = (ctx != NULL) ? ctx->family : 0xFFU;
    cache->rt_type = (ctx != NULL) ? ctx->type : 0xFFU;
    cache->rt_mix_track_id = (ctx != NULL) ? ctx->mix_track_id : 0xFFU;
    cache->valid = 1U;

    return cache;
}

void mod_destination_catalog_invalidate_track(uint8_t track)
{
    if (track < SEQ_TRACK_COUNT)
    {
        g_mod_destination_cache[track].valid = 0U;
    }
}

void mod_destination_catalog_invalidate_all(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        mod_destination_catalog_invalidate_track(track);
    }
}

uint16_t mod_destination_catalog_count(uint8_t track)
{
    mod_destination_cache_t *const cache = mod_destination_cache_resolve(track);
    return (cache != NULL) ? cache->count : 1U;
}

param_id_t mod_destination_catalog_param_from_index(uint8_t track, uint16_t dest_index)
{
    if ((track >= SEQ_TRACK_COUNT) || (dest_index == 0U))
    {
        return MOD_DESTINATION_NONE;
    }

    mod_destination_cache_t *const cache = mod_destination_cache_resolve(track);
    if ((cache == NULL) || (dest_index >= cache->count))
    {
        return MOD_DESTINATION_NONE;
    }

    return cache->index_to_param[dest_index];
}

uint16_t mod_destination_catalog_index_from_param(uint8_t track, param_id_t dest)
{
    if ((track >= SEQ_TRACK_COUNT) || (dest >= PARAM_COUNT))
    {
        return 0U;
    }

    mod_destination_cache_t *const cache = mod_destination_cache_resolve(track);
    return (cache != NULL) ? cache->param_to_index[(uint16_t)dest] : 0U;
}

uint8_t mod_destination_catalog_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return 0U;
    }

    const param_id_t dest = mod_destination_catalog_param_from_index(track, dest_index);
    if (dest == MOD_DESTINATION_NONE)
    {
        out[0] = 'O';
        out[1] = 'f';
        out[2] = 'f';
        out[3] = '\0';
        return 1U;
    }

    if (dest >= PARAM_COUNT)
    {
        return 0U;
    }

    const char *name = NULL;
    if (param_wave_label_for_track_param(track, dest, &name) == 0U)
    {
        name = param_registry[dest].name;
    }
    if (name == NULL)
    {
        return 0U;
    }

    uint32_t i = 0U;
    for (; (i + 1U) < out_len; ++i)
    {
        const char c = name[i];
        out[i] = c;
        if (c == '\0')
        {
            return 1U;
        }
    }
    out[i] = '\0';
    return 1U;
}

static const char *mod_destination_short_label_for_param(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_MIX_LEVEL: return "Lvl";
        case PARAM_MIX_SEND1: return "Snd1";
        case PARAM_MIX_SEND2: return "Snd2";
        case PARAM_FILTER_CUTOFF: return "Cutf";
        case PARAM_FILTER_EG_AMT: return "EG";
        case PARAM_SAMPLER_START: return "Strt";
        case PARAM_SAMPLER_SLICE_COUNT: return "Slic";
        case PARAM_SAMPLER_CLIP_SOURCE_BPM: return "SrcB";
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH: return "Sync";
        case PARAM_SAMPLER_CLIP_PITCH: return "Tune";
        case PARAM_SAMPLER_CLIP_PLAY_MODE: return "Mode";
        case PARAM_SAMPLER_CLIP_STRETCH_MODE: return "Strc";
        case PARAM_SAMPLER_CLIP_GRAIN: return "Gra.";
        default: return NULL;
    }
}

static void mod_destination_copy_short_label(const char *src, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if (src == NULL)
    {
        out[0] = '\0';
        return;
    }

    uint32_t i = 0U;
    for (; (i < 4U) && ((i + 1U) < out_len) && (src[i] != '\0'); ++i)
    {
        out[i] = src[i];
    }
    out[i] = '\0';
}

uint8_t mod_destination_catalog_short_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return 0U;
    }

    const param_id_t dest = mod_destination_catalog_param_from_index(track, dest_index);
    if (dest == MOD_DESTINATION_NONE)
    {
        mod_destination_copy_short_label("Off", out, out_len);
        return 1U;
    }

    if (dest >= PARAM_COUNT)
    {
        return 0U;
    }

    const char *label = NULL;
    if (param_wave_label_for_track_param(track, dest, &label) == 0U)
    {
        label = mod_destination_short_label_for_param(dest);
    }
    if (label == NULL)
    {
        label = param_registry[dest].name;
    }
    if (label == NULL)
    {
        return 0U;
    }

    mod_destination_copy_short_label(label, out, out_len);
    return 1U;
}

void mod_destination_catalog_init(void)
{
    mod_destination_catalog_reset_runtime();
    mod_destination_catalog_invalidate_all();
}

void mod_destination_catalog_reset_runtime(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t i = 0U; i < 12U; ++i)
        {
            g_mod_destination_midi_cc_cache[track][i].valid = 0U;
            g_mod_destination_midi_cc_cache[track][i].value = 0U;
        }
    }
}

void mod_destination_catalog_invalidate_runtime_value(uint8_t track, param_id_t id)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    uint8_t midi_cc_index = 0U;
    if (mod_destination_midi_cc_cache_index(id, &midi_cc_index) != 0U)
    {
        g_mod_destination_midi_cc_cache[track][midi_cc_index].valid = 0U;
    }
}
