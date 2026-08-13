#include "Mod/mod_destination_catalog.h"

#include <stdio.h>
#include "Audio/audio_note_engine_adapter.h"

#include "Audio/audio_xfade.h"
#include "Audio/drum_synth.h"
#include "Audio/md_model.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/synth_polyphony.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/track_runtime.h"
#include "Core/entity_topology.h"
#include "Core/track_tone_sound_state.h"
#include "Param/param_filter.h"
#include "Param/param_registry.h"
#include "Param/param_registry_backends.h"
#include "Param/param_prism_labels.h"
#include "Mod/mod_lfo_v1.h"
#include "Seq/seq_types.h"
#include "mixer.h"

/* Destination catalogs and MIDI-CC caches are lane-scoped. */
#undef SEQ_TRACK_COUNT
#define SEQ_TRACK_COUNT SEQ_LANE_CAPACITY

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
    uint8_t bind_state;
    uint8_t family;
    uint8_t type;
    uint8_t mix_track_id;
    uint8_t audio_routable;
    uint8_t supports_vca_gate;
} mod_destination_context_view_t;

typedef struct
{
    uint8_t valid;
    uint8_t value;
} mod_destination_midi_cc_cache_t;

static mod_destination_cache_t g_mod_destination_cache[SEQ_TRACK_COUNT];
static mod_destination_midi_cc_cache_t g_mod_destination_midi_cc_cache[SEQ_TRACK_COUNT][12U];

mod_destination_address_t mod_destination_address_make(uint8_t entity_id,
                                                       param_id_t param)
{
    if ((entity_id >= BRICK_ENTITY_CAPACITY)
            || ((uint16_t)param > MOD_DESTINATION_PARAM_MASK)
            || (param >= PARAM_COUNT))
    {
        return MOD_DESTINATION_NONE;
    }
    return (mod_destination_address_t)(((uint16_t)entity_id << MOD_DESTINATION_PARAM_BITS)
            | (uint16_t)param);
}

uint8_t mod_destination_address_resolve(mod_destination_address_t address,
                                        uint8_t *out_entity_id,
                                        param_id_t *out_param)
{
    const uint8_t entity_id = (uint8_t)(address >> MOD_DESTINATION_PARAM_BITS);
    const param_id_t param = (param_id_t)(address & MOD_DESTINATION_PARAM_MASK);
    if ((address == MOD_DESTINATION_NONE) || (entity_id >= BRICK_ENTITY_CAPACITY)
            || (param >= PARAM_COUNT) || (out_entity_id == NULL) || (out_param == NULL))
    {
        return 0U;
    }
    *out_entity_id = entity_id;
    *out_param = param;
    return 1U;
}

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

static uint8_t mod_destination_is_poly_filter_voice_local(param_id_t dest)
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
            return 1U;
        default:
            return 0U;
    }
}

static ui_track_family_t mod_destination_family_for_track(
    uint8_t track, const audio_binding_snapshot_t *ctx)
{
    entity_topology_descriptor_t entity;
    if ((entity_topology_get((brick_entity_id_t)track, &entity) != 0U)
            && (entity.active != 0U)
            && (entity.role != ENTITY_ROLE_GROUP_CHILD))
    {
        return ui_get_track_family(track);
    }

    if (ctx == NULL)
    {
        return UI_TRACK_FAMILY_OFF;
    }

    switch ((track_runtime_family_t)ctx->family)
    {
        case TRACK_RUNTIME_FAMILY_SYNTH:
            return UI_TRACK_FAMILY_SYNTH;
        case TRACK_RUNTIME_FAMILY_SAMPLER:
            return UI_TRACK_FAMILY_SAMPLER;
        case TRACK_RUNTIME_FAMILY_DRUM:
            return UI_TRACK_FAMILY_DRUM;
        case TRACK_RUNTIME_FAMILY_MIDI:
            return UI_TRACK_FAMILY_MIDI;
        case TRACK_RUNTIME_FAMILY_EXTERNAL:
            return UI_TRACK_FAMILY_EXTERNAL;
        default:
            return UI_TRACK_FAMILY_OFF;
    }
}

static ui_track_type_t mod_destination_type_for_track(
    uint8_t track, const audio_binding_snapshot_t *ctx)
{
    entity_topology_descriptor_t entity;
    if ((entity_topology_get((brick_entity_id_t)track, &entity) != 0U)
            && (entity.active != 0U)
            && (entity.role != ENTITY_ROLE_GROUP_CHILD))
    {
        return ui_get_track_type(track);
    }

    if ((ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_RAM))
    {
        return UI_TRACK_TYPE_RAM;
    }

    return UI_TRACK_TYPE_NONE;
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

static uint8_t mod_destination_is_structural_sampler(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_SAMPLER_MODE:
        case PARAM_SAMPLER_SLICE_COUNT:
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
        case PARAM_SAMPLER_CLIP_PITCH:
        case PARAM_SAMPLER_CLIP_PLAY_MODE:
        case PARAM_SAMPLER_CLIP_LOOP:
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
        case PARAM_SAMPLER_CLIP_GRAIN:
        case PARAM_SAMPLER_CLIP_HOP:
        case PARAM_SAMPLER_CLIP_SEARCH:
        case PARAM_SAMPLER_MULTI_LOOP:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_direct_prism(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_PRISM_COARSE:
        case PARAM_PRISM_FM:
        case PARAM_PRISM_TIMBRE:
        case PARAM_PRISM_MODULATION:
        case PARAM_PRISM_COLOR:
        case PARAM_PRISM_LEVEL:
        case PARAM_PRISM_OSC2_COARSE:
        case PARAM_PRISM_OSC2_FM:
        case PARAM_PRISM_OSC2_TIMBRE:
        case PARAM_PRISM_OSC2_MODULATION:
        case PARAM_PRISM_OSC2_COLOR:
        case PARAM_PRISM_OSC2_LEVEL:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_direct_stack(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_STACK_OSC1_LEVEL:
        case PARAM_STACK_OSC2_LEVEL:
        case PARAM_STACK_OSC3_LEVEL:
        case PARAM_STACK_NOISE_LEVEL:
        case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC1_TIMBRE:
        case PARAM_STACK_OSC1_COLOR:
        case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC2_TIMBRE:
        case PARAM_STACK_OSC2_COLOR:
        case PARAM_STACK_OSC3_TUNE:
        case PARAM_STACK_OSC3_TIMBRE:
        case PARAM_STACK_OSC3_COLOR:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_direct_wave(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_WAVE_OSC1_POS:
        case PARAM_WAVE_OSC1_LEVEL:
        case PARAM_WAVE_OSC1_TUNE:
        case PARAM_WAVE_OSC2_POS:
        case PARAM_WAVE_OSC2_LEVEL:
        case PARAM_WAVE_OSC2_TUNE:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_direct_fm(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_FM_RATIO:
        case PARAM_FM_BRIGHT:
        case PARAM_FM_BODY:
        case PARAM_FM_DETAIL:
        case PARAM_FM_METAL:
        case PARAM_FM_ENV_ATTACK:
        case PARAM_FM_ENV_DECAY:
        case PARAM_FM_ENV_SUSTAIN:
        case PARAM_FM_ENV_RELEASE:
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
        case PARAM_DRUM_MD_P1:
        case PARAM_DRUM_MD_P2:
        case PARAM_DRUM_MD_P3:
        case PARAM_DRUM_MD_P4:
        case PARAM_DRUM_MD_P5:
        case PARAM_DRUM_MD_P6:
        case PARAM_DRUM_MD_P7:
        case PARAM_DRUM_MD_P8:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_direct_midi_cc(param_id_t dest)
{
    return param_backend_is_midi_cc_id(dest);
}

static uint8_t mod_destination_is_lfo_rate(param_id_t dest)
{
    return ((dest == PARAM_LFO1_RATE) || (dest == PARAM_LFO2_RATE) || (dest == PARAM_LFO3_RATE)) ? 1U : 0U;
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
                                                   const track_audio_runtime_ctx_t *ctx,
                                                   float value)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
            || (ctx->audio_binding.mix_track_id >= MIXER_MAX_TRACKS))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_MIX_LEVEL:
            mixer_set_track_gain(ctx->audio_binding.mix_track_id, mod_destination_clampf(value, 0.0f, 2.0f));
            return 1U;
        case PARAM_MIX_PAN:
            mixer_set_track_pan(ctx->audio_binding.mix_track_id, mod_destination_clampf(value, -1.0f, 1.0f));
            return 1U;
        case PARAM_MIX_SEND1:
            mixer_set_track_send_level(ctx->audio_binding.mix_track_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_MIX_SEND2:
            mixer_set_track_send_level(ctx->audio_binding.mix_track_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_filter_rt(uint8_t track,
                                               param_id_t dest,
                                               const track_audio_runtime_ctx_t *ctx,
                                               float value)
{
    if ((ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
            || (ctx->audio_binding.mix_track_id >= MIXER_MAX_TRACKS)
            || ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL)
                && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_FILTER_CUTOFF:
            mixer_set_track_filter_cutoff_modulated(ctx->audio_binding.mix_track_id,
                                                    param_filter_ui127_to_cutoff_hz(value));
            return 1U;
        case PARAM_FILTER_RESONANCE:
            mixer_set_track_filter_resonance(ctx->audio_binding.mix_track_id, param_filter_ui127_to_resonance(value));
            return 1U;
        case PARAM_FILTER_EG_AMT:
            mixer_set_track_filter_eg_amount(ctx->audio_binding.mix_track_id, param_filter_ui127_to_eg_amount(value));
            return 1U;
        case PARAM_FILTER_ATTACK:
            mixer_set_track_filter_attack(ctx->audio_binding.mix_track_id, param_filter_ui127_to_attack_s(value));
            return 1U;
        case PARAM_FILTER_DECAY:
            mixer_set_track_filter_decay(ctx->audio_binding.mix_track_id, param_filter_ui127_to_decay_s(value));
            return 1U;
        case PARAM_FILTER_SUSTAIN:
            mixer_set_track_filter_sustain(ctx->audio_binding.mix_track_id, param_filter_ui127_to_sustain(value));
            return 1U;
        case PARAM_FILTER_RELEASE:
            mixer_set_track_filter_release(ctx->audio_binding.mix_track_id, param_filter_ui127_to_release_s(value));
            return 1U;
        case PARAM_FILTER_KEYTRK:
            mixer_set_track_filter_keytrack(ctx->audio_binding.mix_track_id, param_filter_ui127_to_keytrack(value));
            return 1U;
        case PARAM_FILTER_EQ_LOW:
            mixer_set_track_filter_eq_low(ctx->audio_binding.mix_track_id, param_filter_eq_ui127_to_db(value));
            return 1U;
        case PARAM_FILTER_EQ_MID:
            mixer_set_track_filter_eq_mid(ctx->audio_binding.mix_track_id, param_filter_eq_ui127_to_db(value));
            return 1U;
        case PARAM_FILTER_EQ_HIGH:
            mixer_set_track_filter_eq_high(ctx->audio_binding.mix_track_id, param_filter_eq_ui127_to_db(value));
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_vca_rt(uint8_t track,
                                            param_id_t dest,
                                            const track_audio_runtime_ctx_t *ctx,
                                            float value)
{
    if ((ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
            || (ctx->audio_binding.mix_track_id >= MIXER_MAX_TRACKS)
            || (audio_note_engine_adapter_ctx_supports_vca_gate(ctx) == 0U))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_VCA_ATTACK:
            mixer_set_track_vca_attack(ctx->audio_binding.mix_track_id, param_filter_ui127_to_attack_s(value));
            return 1U;
        case PARAM_VCA_DECAY:
            mixer_set_track_vca_decay(ctx->audio_binding.mix_track_id, param_filter_ui127_to_decay_s(value));
            return 1U;
        case PARAM_VCA_SUSTAIN:
            mixer_set_track_vca_sustain(ctx->audio_binding.mix_track_id, param_filter_ui127_to_sustain(value));
            return 1U;
        case PARAM_VCA_RELEASE:
        {
            const float release_s = param_filter_ui127_to_release_s(value);
            mixer_set_track_vca_release(ctx->audio_binding.mix_track_id, release_s);
            if (ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            {
                brick6_braids_runtime_set_vca_release_seconds(ctx->audio_binding.instance_id, release_s);
            }
            return 1U;
        }
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_sampler_rt(uint8_t track,
                                                param_id_t dest,
                                                const track_audio_runtime_ctx_t *ctx,
                                                float value)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
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
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_RAM) { return 0U; }
            brick6_sampler_runtime_set_start(track, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_END:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_RAM) { return 0U; }
            brick6_sampler_runtime_set_end(track, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_LOOP_START:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_RAM) { return 0U; }
            brick6_sampler_runtime_set_loop_start(track, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_MODE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_RAM) { return 0U; }
            brick6_sampler_runtime_set_mode(track, (uint8_t)(mod_destination_clampf(value, 0.0f, 3.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_TUNE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_RAM) { return 0U; }
            brick6_sampler_runtime_set_tune(track, mod_destination_clampf(value, -24.0f, 24.0f));
            return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
        {
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_RAM) { return 0U; }
            static const uint8_t counts[] = {0U, 2U, 4U, 8U, 16U, 32U, 64U};
            const uint8_t idx = (uint8_t)(mod_destination_clampf(value, 0.0f, 6.0f) + 0.5f);
            brick6_sampler_runtime_set_slice_count(track, counts[idx]);
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM) { return 0U; }
            brick6_sampler_runtime_set_clip_source_bpm(track, mod_destination_clampf(value, 40.0f, 300.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM) { return 0U; }
            brick6_sampler_runtime_set_clip_sync_length(track, (uint8_t)(mod_destination_clampf(value, 0.0f, 4.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_PITCH:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM) { return 0U; }
            brick6_sampler_runtime_set_clip_pitch(track, mod_destination_clampf(value, -12.0f, 12.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_PLAY_MODE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM) { return 0U; }
            brick6_sampler_runtime_set_clip_play_mode(track, (uint8_t)(mod_destination_clampf(value, 0.0f, 1.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_LOOP:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM) { return 0U; }
            brick6_sampler_runtime_set_clip_loop(track, (uint8_t)(mod_destination_clampf(value, 0.0f, 1.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM) { return 0U; }
            brick6_sampler_runtime_set_clip_stretch_mode(track, (uint8_t)(mod_destination_clampf(value, 0.0f, 2.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_GRAIN:
        {
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM) { return 0U; }
            static const uint16_t grain_frames[] = {384U, 512U, 768U, 1024U, 1536U, 2048U};
            const uint8_t idx = (uint8_t)(mod_destination_clampf(value, 0.0f, 5.0f) + 0.5f);
            brick6_sampler_runtime_set_clip_grain_size(track, grain_frames[idx]);
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_HOP:
        case PARAM_SAMPLER_CLIP_SEARCH:
            return (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_STREAM) ? 1U : 0U;
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

static uint8_t mod_destination_apply_prism_rt(uint8_t track,
                                             param_id_t dest,
                                             const track_audio_runtime_ctx_t *ctx,
                                             float value)
{
    (void)track;

    if ((ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->audio_binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_PRISM))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_PRISM_COARSE:
            brick6_braids_runtime_set_osc_coarse(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_FM:
            brick6_braids_runtime_set_osc_fm(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_TIMBRE:
            brick6_braids_runtime_set_osc_timbre(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_MODULATION:
            brick6_braids_runtime_set_osc_modulation(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_COLOR:
            brick6_braids_runtime_set_osc_color(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_LEVEL:
            brick6_braids_runtime_set_osc_level(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_COARSE:
            brick6_braids_runtime_set_osc_coarse(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_FM:
            brick6_braids_runtime_set_osc_fm(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_TIMBRE:
            brick6_braids_runtime_set_osc_timbre(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_MODULATION:
            brick6_braids_runtime_set_osc_modulation(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_COLOR:
            brick6_braids_runtime_set_osc_color(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_LEVEL:
            brick6_braids_runtime_set_osc_level(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_fm_rt(uint8_t track,
                                          param_id_t dest,
                                          const track_audio_runtime_ctx_t *ctx,
                                          float value)
{
    if ((ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->audio_binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_FM)
            || (mod_destination_is_direct_fm(dest) == 0U))
    {
        return 0U;
    }
    return param_registry_apply_track_value_rt_fast(dest, track, value);
}

static uint8_t mod_destination_stack_slot_for_id(param_id_t id, uint8_t *out_slot, uint8_t *out_param)
{
    if ((out_slot == NULL) || (out_param == NULL))
    {
        return 0U;
    }

    if ((id >= PARAM_STACK_OSC1_LEVEL) && (id <= PARAM_STACK_OSC3_LEVEL))
    {
        *out_slot = (uint8_t)(id - PARAM_STACK_OSC1_LEVEL);
        *out_param = 0U;
        return 1U;
    }
    if ((id >= PARAM_STACK_OSC1_MODEL) && (id <= PARAM_STACK_OSC3_COLOR))
    {
        const uint8_t rel = (uint8_t)(id - PARAM_STACK_OSC1_MODEL);
        *out_slot = (uint8_t)(rel / 4U);
        *out_param = (uint8_t)((rel % 4U) + 1U);
        return (*out_slot < BRICK6_STACK_SLOT_COUNT) ? 1U : 0U;
    }

    return 0U;
}

static const char *mod_destination_stack_model_label(uint8_t model, uint8_t slot_param)
{
    switch (slot_param)
    {
        case 3U:
            switch ((brick6_stack_model_t)model)
            {
                case BRICK6_STACK_MODEL_SHAPE: return "SHAPE";
                case BRICK6_STACK_MODEL_TRIPLE_SAW: return "OSC2";
                case BRICK6_STACK_MODEL_SQUARE: return "PWM";
                default: return NULL;
            }
        case 4U:
            switch ((brick6_stack_model_t)model)
            {
                case BRICK6_STACK_MODEL_SHAPE: return "MORPH";
                case BRICK6_STACK_MODEL_TRIPLE_SAW: return "OSC3";
                default: return NULL;
            }
        default:
            return NULL;
    }
}

static uint8_t mod_destination_stack_label_for_track_param(uint8_t track, param_id_t dest, const char **out_label)
{
    if (out_label == NULL)
    {
        return 0U;
    }

    uint8_t slot = 0U;
    uint8_t slot_param = 0U;
    if ((mod_destination_stack_slot_for_id(dest, &slot, &slot_param) == 0U)
            || (slot >= BRICK6_STACK_SLOT_COUNT)
            || (slot_param < 3U)
            || (slot_param > 4U))
    {
        return 0U;
    }

    const track_tone_sound_state_t *const tone = track_tone_sound_state_get_const(track);
    if (tone == NULL)
    {
        return 0U;
    }

    uint8_t model = (uint8_t)(tone->stack.model[slot] + 0.5f);
    if (model >= (uint8_t)BRICK6_STACK_MODEL_COUNT)
    {
        model = (uint8_t)BRICK6_STACK_MODEL_SHAPE;
    }

    *out_label = mod_destination_stack_model_label(model, slot_param);
    return (*out_label != NULL) ? 1U : 0U;
}

static uint8_t mod_destination_apply_stack_rt(uint8_t track,
                                              param_id_t dest,
                                              const track_audio_runtime_ctx_t *ctx,
                                              float value)
{
    (void)track;

    if ((ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->audio_binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            || (ctx->audio_binding.instance_id >= BRICK6_STACK_MAX_INSTANCES))
    {
        return 0U;
    }

    if (dest == PARAM_STACK_NOISE_LEVEL)
    {
        brick6_stack_runtime_set_noise_level(ctx->audio_binding.instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
        return 1U;
    }
    uint8_t slot = 0U;
    uint8_t slot_param = 0U;
    if (mod_destination_stack_slot_for_id(dest, &slot, &slot_param) == 0U)
    {
        return 0U;
    }

    switch (slot_param)
    {
        case 0U:
            brick6_stack_runtime_set_slot_level(ctx->audio_binding.instance_id, slot, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case 2U:
        {
            const float clamped = mod_destination_clampf(value, -24.0f, 24.0f);
            brick6_stack_runtime_set_slot_tune(ctx->audio_binding.instance_id,
                                               slot,
                                               clamped);
            return 1U;
        }
        case 3U:
            brick6_stack_runtime_set_slot_timbre(ctx->audio_binding.instance_id, slot, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case 4U:
            brick6_stack_runtime_set_slot_color(ctx->audio_binding.instance_id, slot, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_wave_rt(uint8_t track,
                                             param_id_t dest,
                                             const track_audio_runtime_ctx_t *ctx,
                                             float value)
{
    (void)track;

    if ((ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->audio_binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            || (ctx->audio_binding.instance_id >= BRICK6_WAVE_MAX_INSTANCES))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_WAVE_OSC1_POS:
            brick6_wave_runtime_set_osc_pos(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC1_LEVEL:
            brick6_wave_runtime_set_osc_level(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC1_TUNE:
            brick6_wave_runtime_set_osc_tune(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, -60.0f, 60.0f));
            return 1U;
        case PARAM_WAVE_OSC2_POS:
            brick6_wave_runtime_set_osc_pos(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC2_LEVEL:
            brick6_wave_runtime_set_osc_level(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC2_TUNE:
            brick6_wave_runtime_set_osc_tune(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, -60.0f, 60.0f));
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_drum_rt(uint8_t track,
                                             param_id_t dest,
                                             const track_audio_runtime_ctx_t *ctx,
                                             float value)
{
    (void)track;

    if ((ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->audio_binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
            || ((ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG)
                && (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_DRUM_MD)))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_DRUM_MD_P1:
        case PARAM_DRUM_MD_P2:
        case PARAM_DRUM_MD_P3:
        case PARAM_DRUM_MD_P4:
        case PARAM_DRUM_MD_P5:
        case PARAM_DRUM_MD_P6:
        case PARAM_DRUM_MD_P7:
        case PARAM_DRUM_MD_P8:
            return drum_synth_set_param_for_instance(ctx->audio_binding.instance_id,
                                                     dest,
                                                     mod_destination_clampf(value, 0.0f, 127.0f));
        case PARAM_DRUM_TRX_BD_PITCH:
            return drum_synth_set_param_for_instance(ctx->audio_binding.instance_id,
                                                     dest,
                                                     mod_destination_clampf(value, -48.0f, 24.0f));
        case PARAM_DRUM_TRX_BD_DECAY:
            return drum_synth_set_param_for_instance(ctx->audio_binding.instance_id,
                                                     dest,
                                                     mod_destination_clampf(value, 0.01f, 2.0f));
        case PARAM_DRUM_TRX_BD_HARMONICS:
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
            return drum_synth_set_param_for_instance(ctx->audio_binding.instance_id,
                                                     dest,
                                                     mod_destination_clampf(value, 0.0f, 1.0f));
        default:
            return 0U;
    }
}

static uint8_t mod_destination_apply_midi_cc_rt(uint8_t track,
                                                param_id_t dest,
                                                const track_audio_runtime_ctx_t *ctx,
                                                float value)
{
    const uint8_t midi_track = ((ctx != NULL) && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI)) ? 1U : 0U;
    if ((track >= SEQ_TRACK_COUNT)
            || (ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (midi_track == 0U)
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
                                         const track_audio_runtime_ctx_t *ctx,
                                         float value)
{
    if (mod_destination_is_structural_sampler(dest) != 0U)
    {
        return 0U;
    }
    if (mod_destination_is_lfo_rate(dest) != 0U)
    {
        (void)ctx;
        return mod_lfo_v1_apply_track_param_temp(track,
                                                 (uint8_t)(dest - PARAM_LFO1_RATE) / (uint8_t)MOD_LFO_PARAM_COUNT,
                                                 MOD_LFO_PARAM_RATE,
                                                 value);
    }
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
    if (mod_destination_is_direct_prism(dest) != 0U)
    {
        return mod_destination_apply_prism_rt(track, dest, ctx, value);
    }
    if (mod_destination_is_direct_fm(dest) != 0U)
    {
        return mod_destination_apply_fm_rt(track, dest, ctx, value);
    }
    if (mod_destination_is_direct_stack(dest) != 0U)
    {
        return mod_destination_apply_stack_rt(track, dest, ctx, value);
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

uint8_t mod_destination_catalog_apply_poly_voice_rt(uint8_t track,
                                                    uint8_t voice_slot,
                                                    param_id_t dest,
                                                    const track_audio_runtime_ctx_t *ctx,
                                                    float value)
{
    if ((ctx == NULL) || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    track_audio_runtime_ctx_t voice_ctx = *ctx;
    voice_ctx.audio_binding.instance_id = voice_slot;
    if ((ctx->audio_binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            && (mod_destination_is_direct_filter(dest) != 0U
                || mod_destination_is_direct_vca(dest) != 0U))
    {
        switch (dest)
        {
            case PARAM_FILTER_CUTOFF:
            {
                const float cutoff_hz = param_filter_ui127_to_cutoff_hz(value);
                mixer_poly_voice_set_cutoff(voice_slot, cutoff_hz);
                return 1U;
            }
            case PARAM_FILTER_RESONANCE:
                mixer_poly_voice_set_resonance(voice_slot, param_filter_ui127_to_resonance(value)); return 1U;
            case PARAM_FILTER_EG_AMT:
                mixer_poly_voice_set_eg_amount(voice_slot, param_filter_ui127_to_eg_amount(value)); return 1U;
            case PARAM_FILTER_ATTACK:
                mixer_poly_voice_set_filter_attack(voice_slot, param_filter_ui127_to_attack_s(value)); return 1U;
            case PARAM_FILTER_DECAY:
                mixer_poly_voice_set_filter_decay(voice_slot, param_filter_ui127_to_decay_s(value)); return 1U;
            case PARAM_FILTER_SUSTAIN:
                mixer_poly_voice_set_filter_sustain(voice_slot, param_filter_ui127_to_sustain(value)); return 1U;
            case PARAM_FILTER_RELEASE:
                mixer_poly_voice_set_filter_release(voice_slot, param_filter_ui127_to_release_s(value)); return 1U;
            case PARAM_VCA_ATTACK:
                mixer_poly_voice_set_vca_attack(voice_slot, param_filter_ui127_to_attack_s(value)); return 1U;
            case PARAM_VCA_DECAY:
                mixer_poly_voice_set_vca_decay(voice_slot, param_filter_ui127_to_decay_s(value)); return 1U;
            case PARAM_VCA_SUSTAIN:
                mixer_poly_voice_set_vca_sustain(voice_slot, param_filter_ui127_to_sustain(value)); return 1U;
            case PARAM_VCA_RELEASE:
                mixer_poly_voice_set_vca_release(voice_slot, param_filter_ui127_to_release_s(value)); return 1U;
            default: return 0U;
        }
    }
    switch ((track_runtime_engine_t)ctx->audio_binding.engine)
    {
        case TRACK_RUNTIME_ENGINE_PRISM:
            return (mod_destination_is_direct_prism(dest) != 0U)
                ? mod_destination_apply_prism_rt(track, dest, &voice_ctx, value) : 0U;
        case TRACK_RUNTIME_ENGINE_STACK:
            return (mod_destination_is_direct_stack(dest) != 0U)
                ? mod_destination_apply_stack_rt(track, dest, &voice_ctx, value) : 0U;
        case TRACK_RUNTIME_ENGINE_WAVE:
            return (mod_destination_is_direct_wave(dest) != 0U)
                ? mod_destination_apply_wave_rt(track, dest, &voice_ctx, value) : 0U;
        case TRACK_RUNTIME_ENGINE_SAMPLER:
        {
            if ((ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_MULTI)
                    || (voice_slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET))
            {
                return 0U;
            }
            struct multi_voice_dsp_slot_t *const slot =
                brick6_sampler_runtime_get_multi_voice_dsp(
                    (uint8_t)(voice_slot - SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET));
            if (slot == NULL) return 0U;
            switch (dest)
            {
                case PARAM_FILTER_CUTOFF:
                    mixer_multi_filter_set_voice_cutoff(slot, param_filter_ui127_to_cutoff_hz(value));
                    return 1U;
                case PARAM_FILTER_RESONANCE:
                    mixer_multi_filter_set_voice_resonance(slot, param_filter_ui127_to_resonance(value));
                    return 1U;
                case PARAM_FILTER_EG_AMT:
                    mixer_multi_filter_set_voice_eg_amount(slot, param_filter_ui127_to_eg_amount(value));
                    return 1U;
                case PARAM_FILTER_ATTACK:
                    mixer_multi_filter_set_voice_env_attack(slot, param_filter_ui127_to_attack_s(value));
                    return 1U;
                case PARAM_FILTER_DECAY:
                    mixer_multi_filter_set_voice_env_decay(slot, param_filter_ui127_to_decay_s(value));
                    return 1U;
                case PARAM_FILTER_SUSTAIN:
                    mixer_multi_filter_set_voice_env_sustain(slot, param_filter_ui127_to_sustain(value));
                    return 1U;
                case PARAM_FILTER_RELEASE:
                    mixer_multi_filter_set_voice_env_release(slot, param_filter_ui127_to_release_s(value));
                    return 1U;
                case PARAM_VCA_ATTACK:
                    mixer_multi_filter_set_voice_vca_attack(slot, param_filter_ui127_to_attack_s(value));
                    return 1U;
                case PARAM_VCA_DECAY:
                    mixer_multi_filter_set_voice_vca_decay(slot, param_filter_ui127_to_decay_s(value));
                    return 1U;
                case PARAM_VCA_SUSTAIN:
                    mixer_multi_filter_set_voice_vca_sustain(slot, param_filter_ui127_to_sustain(value));
                    return 1U;
                case PARAM_VCA_RELEASE:
                    mixer_multi_filter_set_voice_vca_release(slot, param_filter_ui127_to_release_s(value));
                    return 1U;
                default:
                    return 0U;
            }
        }
        default:
            return 0U;
    }
}

uint8_t mod_destination_catalog_poly_voice_supported(param_id_t dest,
                                                      const track_audio_runtime_ctx_t *ctx)
{
    if (ctx == NULL) return 0U;
    if ((mod_destination_is_poly_filter_voice_local(dest) != 0U)
            || (mod_destination_is_direct_vca(dest) != 0U))
    {
        return ((ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
                || (ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
                || (ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
                || ((ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
                    && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))) ? 1U : 0U;
    }
    switch ((track_runtime_engine_t)ctx->audio_binding.engine)
    {
        case TRACK_RUNTIME_ENGINE_PRISM: return mod_destination_is_direct_prism(dest);
        case TRACK_RUNTIME_ENGINE_STACK: return mod_destination_is_direct_stack(dest);
        case TRACK_RUNTIME_ENGINE_WAVE: return mod_destination_is_direct_wave(dest);
        case TRACK_RUNTIME_ENGINE_FM: return 0U;
        default: return 0U;
    }
}

static uint8_t mod_destination_is_continuous_rampable(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_MIX_LEVEL:
        case PARAM_MIX_PAN:
        case PARAM_MIX_SEND1:
        case PARAM_MIX_SEND2:
        case PARAM_FILTER_CUTOFF:
        case PARAM_FILTER_RESONANCE:
        case PARAM_SAMPLER_GAIN:
        case PARAM_SAMPLER_TUNE:
        case PARAM_PRISM_COARSE:
        case PARAM_PRISM_LEVEL:
        case PARAM_PRISM_OSC2_COARSE:
        case PARAM_PRISM_OSC2_LEVEL:
        case PARAM_STACK_OSC1_LEVEL:
        case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC2_LEVEL:
        case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC3_LEVEL:
        case PARAM_STACK_OSC3_TUNE:
        case PARAM_WAVE_OSC1_POS:
        case PARAM_WAVE_OSC1_LEVEL:
        case PARAM_WAVE_OSC1_TUNE:
        case PARAM_WAVE_OSC2_POS:
        case PARAM_WAVE_OSC2_LEVEL:
        case PARAM_WAVE_OSC2_TUNE:
        case PARAM_DRUM_TRX_BD_PITCH:
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_is_segment_rate(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_SAMPLER_TUNE:
        case PARAM_PRISM_COARSE:
        case PARAM_PRISM_OSC2_COARSE:
        case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC3_TUNE:
        case PARAM_WAVE_OSC1_TUNE:
        case PARAM_WAVE_OSC2_TUNE:
        case PARAM_DRUM_TRX_BD_PITCH:
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
            return 1U;
        default:
            return 0U;
    }
}

uint8_t mod_destination_catalog_apply_ramp_rt(uint8_t track,
                                              param_id_t dest,
                                              const track_audio_runtime_ctx_t *ctx,
                                              const mod_destination_ramp_t *ramp)
{
    if (ramp == NULL)
    {
        return 0U;
    }

    if ((ramp->discontinuous == 0U)
            && (ramp->frames > 1U)
            && (mod_destination_is_segment_rate(dest) != 0U))
    {
        return mod_destination_catalog_apply_rt(track, dest, ctx, ramp->end);
    }

    const uint8_t applied = mod_destination_catalog_apply_rt(track,
                                                              dest,
                                                              ctx,
                                                              ramp->current);
    if ((applied != 0U)
            && (ramp->discontinuous == 0U)
            && (ramp->frames > 1U)
            && (mod_destination_is_continuous_rampable(dest) != 0U))
    {
        return mod_destination_catalog_apply_rt(track, dest, ctx, ramp->end);
    }
    return applied;
}

static uint8_t mod_destination_is_internal_lfo_param(param_id_t id)
{
    switch (id)
    {
        case PARAM_LFO1_SHAPE:
        case PARAM_LFO1_TRIG:
        case PARAM_LFO1_PHASE:
        case PARAM_LFO2_SHAPE:
        case PARAM_LFO2_TRIG:
        case PARAM_LFO2_PHASE:
        case PARAM_LFO3_SHAPE:
        case PARAM_LFO3_TRIG:
        case PARAM_LFO3_PHASE:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_param_matches_track_context(uint8_t track,
                                                           ui_track_family_t family,
                                                           param_id_t dest,
                                                           track_runtime_param_domain_t domain,
                                                           const mod_destination_context_view_t *ctx)
{
    if (domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
    {
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
        {
            return 0U;
        }
        if ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_WAVE)
        {
            return mod_destination_is_direct_wave(dest);
        }
        if ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_FM)
        {
            return mod_destination_is_direct_fm(dest);
        }
        if ((dest == PARAM_LOOPER_ARM)
                || (dest == PARAM_LOOPER_LEN)
                || (dest == PARAM_LOOPER_PLAY)
                || (dest == PARAM_LOOPER_STRETCH)
                || (dest == PARAM_LOOPER_PITCH)
                || (dest == PARAM_LOOPER_GRAIN)
                || (dest == PARAM_SAMPLER_SAMPLE)
                || (dest == PARAM_SAMPLER_CLIP_SEARCH)
                || (dest == PARAM_DRUM_MD_MODEL)
                || (((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_DRUM_MD)
                    && (dest >= PARAM_DRUM_TRX_BD_PITCH)
                    && (dest <= PARAM_DRUM_TRX_BD_DRIVE)))
        {
            return 0U;
        }
        if (((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_DRUM_MD)
                && (dest >= PARAM_DRUM_MD_P1)
                && (dest <= PARAM_DRUM_MD_P8))
        {
            const track_tone_sound_state_t *const tone = track_tone_sound_state_get_const(track);
            if (tone == NULL)
            {
                return 0U;
            }
            const md_model_profile_t *const profile = md_model_profile_get(md_model_validate(tone->md.model));
            return (uint8_t)((uint8_t)(dest - PARAM_DRUM_MD_P1) < profile->slot_count);
        }
        if ((dest == PARAM_PRISM_EDIT)
                || (dest == PARAM_PRISM_FINE)
                || (dest == PARAM_PRISM_PHASE_RESET)
                || (dest == PARAM_PRISM_OSC2_EDIT)
                || (dest == PARAM_PRISM_OSC2_FINE)
                || (dest == PARAM_PRISM_OSC2_PHASE_RESET)
                || (dest == PARAM_MIDI_PROGRAM)
                || (dest == PARAM_STACK_OSC_DETUNE)
                || (dest == PARAM_STACK_PHASE_RESET)
                || (dest == PARAM_STACK_OSC1_MODEL)
                || (dest == PARAM_STACK_OSC2_MODEL)
                || (dest == PARAM_STACK_OSC3_MODEL))
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

    if (domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
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

        if (mod_destination_is_direct_vca(dest) != 0U)
        {
            return ((ctx != NULL) && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND))
                ? ctx->supports_vca_gate
                : 0U;
        }

        return (((dest >= PARAM_FILTER_TYPE) && (dest <= PARAM_FILTER_ENVDLY))
                || ((dest >= PARAM_FILTER_EQ_LOW) && (dest <= PARAM_FILTER_EQ_HIGH))
                || ((dest >= PARAM_ENV3_ATTACK) && (dest <= PARAM_ENV3_RELEASE))) ? 1U : 0U;
    }

    if (domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
    {
        if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
        {
            return 0U;
        }

        return ((dest == PARAM_MIX_LEVEL)
                || (dest == PARAM_MIX_PAN)
                || (dest == PARAM_MIX_SEND1)
                || (dest == PARAM_MIX_SEND2)) ? 1U : 0U;
    }

    return 0U;
}

static track_runtime_param_status_t mod_destination_effective_status_from_ctx(const mod_destination_context_view_t *ctx,
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
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_OFF)
                    || (ctx->audio_routable == 0U))
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
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI)
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL))
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
        case TRACK_RUNTIME_RESOURCE_MIX:
            if ((ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                    || (ctx->audio_routable == 0U)
                    || (ctx->mix_track_id >= SEQ_MAIN_TRACK_COUNT))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return TRACK_RUNTIME_PARAM_ALLOWED;
        default:
            return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }
}

static uint8_t mod_destination_catalog_supported_view(
    uint8_t track, param_id_t dest, ui_track_family_t family,
    ui_track_type_t type, const mod_destination_context_view_t *context)
{
    (void)type;

    if ((track >= SEQ_TRACK_COUNT)
            || (dest >= PARAM_COUNT)
            || (mod_destination_is_internal_lfo_param(dest) != 0U)
            || (mod_destination_is_structural_sampler(dest) != 0U))
    {
        return 0U;
    }

    if (mod_destination_is_lfo_rate(dest) != 0U)
    {
        (void)family;
        (void)type;
        return (context != NULL) ? 1U : 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(dest);
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG)
    {
        return 0U;
    }
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_ENV)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX))
    {
        return 0U;
    }

    if (mod_destination_param_matches_track_context(track, family, dest,
                                                    rule.domain, context) == 0U)
    {
        return 0U;
    }

    const track_runtime_param_status_t status =
        mod_destination_effective_status_from_ctx(context, rule.resource);
    return ((status == TRACK_RUNTIME_PARAM_ALLOWED) || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

uint8_t mod_destination_catalog_supported_fast(uint8_t track,
                                               param_id_t dest,
                                               ui_track_family_t family,
                                               ui_track_type_t type,
                                               const track_audio_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
        return mod_destination_catalog_supported_view(
            track, dest, family, type, NULL);
    const mod_destination_context_view_t view = {
        .bind_state = ctx->audio_binding.bind_state,
        .family = ctx->family,
        .type = ctx->type,
        .mix_track_id = ctx->audio_binding.mix_track_id,
        .audio_routable = audio_note_engine_adapter_ctx_is_audio_routable(ctx),
        .supports_vca_gate = audio_note_engine_adapter_ctx_supports_vca_gate(ctx)
    };
    return mod_destination_catalog_supported_view(
        track, dest, family, type, &view);
}

static uint8_t mod_destination_cache_matches_context(const mod_destination_cache_t *cache,
                                                     ui_track_family_t family,
                                                     ui_track_type_t type,
                                                     const audio_binding_snapshot_t *snapshot)
{
    if ((cache == NULL) || (cache->valid == 0U))
    {
        return 0U;
    }

    const uint8_t ctx_bind_state = (snapshot != NULL) ? snapshot->binding.bind_state : 0xFFU;
    const uint8_t ctx_family = (snapshot != NULL) ? snapshot->family : 0xFFU;
    const uint8_t ctx_type = (snapshot != NULL) ? snapshot->type : 0xFFU;
    const uint8_t ctx_mix_track_id = (snapshot != NULL) ? snapshot->binding.mix_track_id : 0xFFU;

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

    audio_binding_snapshot_t snapshot;
    if (audio_note_engine_adapter_snapshot_read(track, &snapshot) == 0U)
        return NULL;
    const ui_track_family_t family =
        mod_destination_family_for_track(track, &snapshot);
    const ui_track_type_t type =
        mod_destination_type_for_track(track, &snapshot);
    const mod_destination_context_view_t view = {
        .bind_state = snapshot.binding.bind_state,
        .family = snapshot.family,
        .type = snapshot.type,
        .mix_track_id = snapshot.binding.mix_track_id,
        .audio_routable = (uint8_t)(
            (snapshot.binding.bind_state == TRACK_RUNTIME_BIND_BOUND)
            && (snapshot.binding.mix_track_id != 0xFFU)
            ),
        .supports_vca_gate = (uint8_t)(
            (snapshot.binding.bind_state == TRACK_RUNTIME_BIND_BOUND)
            && ((snapshot.family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                || (snapshot.family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                || (snapshot.family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)))
    };
    mod_destination_cache_t *const cache = &g_mod_destination_cache[track];

    if (mod_destination_cache_matches_context(
            cache, family, type, &snapshot) != 0U)
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
        if (mod_destination_catalog_supported_view(
                track, param, family, type, &view) == 0U)
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
    cache->rt_bind_state = snapshot.binding.bind_state;
    cache->rt_family = snapshot.family;
    cache->rt_type = snapshot.type;
    cache->rt_mix_track_id = snapshot.binding.mix_track_id;
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
    entity_topology_descriptor_t owner;
    if ((entity_topology_get(track, &owner) != 0U)
            && (owner.role == ENTITY_ROLE_GROUP_MASTER))
    {
        uint16_t count = 1U;
        for (uint8_t target = BRICK_ENTITY_GROUP_MASTER_ID;
             target < BRICK_ENTITY_CAPACITY; ++target)
        {
            mod_destination_cache_t *const target_cache =
                mod_destination_cache_resolve(target);
            if ((target_cache != NULL) && (target_cache->count > 1U))
            {
                count = (uint16_t)(count + target_cache->count - 1U);
            }
        }
        return count;
    }
    mod_destination_cache_t *const cache = mod_destination_cache_resolve(track);
    return (cache != NULL) ? cache->count : 1U;
}

mod_destination_address_t mod_destination_catalog_address_from_index(uint8_t owner,
                                                                     uint16_t dest_index)
{
    if ((owner >= SEQ_TRACK_COUNT) || (dest_index == 0U))
    {
        return MOD_DESTINATION_NONE;
    }

    entity_topology_descriptor_t descriptor;
    if ((entity_topology_get(owner, &descriptor) != 0U)
            && (descriptor.role == ENTITY_ROLE_GROUP_MASTER))
    {
        uint16_t cursor = 1U;
        for (uint8_t target = BRICK_ENTITY_GROUP_MASTER_ID;
             target < BRICK_ENTITY_CAPACITY; ++target)
        {
            mod_destination_cache_t *const cache = mod_destination_cache_resolve(target);
            const uint16_t target_count = ((cache != NULL) && (cache->count > 1U))
                ? (uint16_t)(cache->count - 1U) : 0U;
            if (dest_index < (uint16_t)(cursor + target_count))
            {
                return mod_destination_address_make(
                    target, cache->index_to_param[dest_index - cursor + 1U]);
            }
            cursor = (uint16_t)(cursor + target_count);
        }
        return MOD_DESTINATION_NONE;
    }

    const param_id_t param = mod_destination_catalog_param_from_index(owner, dest_index);
    return mod_destination_address_make(owner, param);
}

uint16_t mod_destination_catalog_index_from_address(uint8_t owner,
                                                    mod_destination_address_t address)
{
    uint8_t target = 0U;
    param_id_t param = PARAM_COUNT;
    if ((mod_destination_address_resolve(address, &target, &param) == 0U)
            || (owner >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    entity_topology_descriptor_t descriptor;
    if ((entity_topology_get(owner, &descriptor) != 0U)
            && (descriptor.role == ENTITY_ROLE_GROUP_MASTER))
    {
        uint16_t cursor = 1U;
        for (uint8_t candidate = BRICK_ENTITY_GROUP_MASTER_ID;
             candidate < BRICK_ENTITY_CAPACITY; ++candidate)
        {
            mod_destination_cache_t *const cache = mod_destination_cache_resolve(candidate);
            if (cache == NULL)
            {
                continue;
            }
            if (candidate == target)
            {
                const uint16_t local_index = cache->param_to_index[(uint16_t)param];
                return (local_index != 0U)
                    ? (uint16_t)(cursor + local_index - 1U) : 0U;
            }
            if (cache->count > 1U)
            {
                cursor = (uint16_t)(cursor + cache->count - 1U);
            }
        }
        return 0U;
    }

    return (target == owner) ? mod_destination_catalog_index_from_param(owner, param) : 0U;
}

param_id_t mod_destination_catalog_param_from_index(uint8_t track, uint16_t dest_index)
{
    entity_topology_descriptor_t descriptor;
    if ((entity_topology_get(track, &descriptor) != 0U)
            && (descriptor.role == ENTITY_ROLE_GROUP_MASTER))
    {
        uint8_t target = 0U;
        param_id_t param = PARAM_COUNT;
        return (mod_destination_address_resolve(
            mod_destination_catalog_address_from_index(track, dest_index),
            &target, &param) != 0U) ? param : MOD_DESTINATION_NONE;
    }
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

static const char *mod_destination_wave_label_for_param(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_WAVE_OSC1_POS: return "OSC1 POS";
        case PARAM_WAVE_OSC1_LEVEL: return "OSC1 LEVEL";
        case PARAM_WAVE_OSC1_TUNE: return "OSC1 TUNE";
        case PARAM_WAVE_OSC2_POS: return "OSC2 POS";
        case PARAM_WAVE_OSC2_LEVEL: return "OSC2 LEVEL";
        case PARAM_WAVE_OSC2_TUNE: return "OSC2 TUNE";
        default: return NULL;
    }
}

static const char *mod_destination_prism_label_for_param(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_PRISM_TIMBRE: return "OSC1 PARAM1";
        case PARAM_PRISM_COLOR: return "OSC1 PARAM2";
        case PARAM_PRISM_MODULATION: return "OSC1 AMOD";
        case PARAM_PRISM_COARSE: return "OSC1 TUNE";
        case PARAM_PRISM_FM: return "OSC1 FM AMT";
        case PARAM_PRISM_LEVEL: return "OSC1 LVL";
        case PARAM_PRISM_OSC2_TIMBRE: return "OSC2 PARAM1";
        case PARAM_PRISM_OSC2_COLOR: return "OSC2 PARAM2";
        case PARAM_PRISM_OSC2_MODULATION: return "OSC2 AMOD";
        case PARAM_PRISM_OSC2_COARSE: return "OSC2 TUNE";
        case PARAM_PRISM_OSC2_FM: return "OSC2 FM AMT";
        case PARAM_PRISM_OSC2_LEVEL: return "OSC2 LVL";
        default: return NULL;
    }
}

uint8_t mod_destination_catalog_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return 0U;
    }

    uint8_t target = track;
    param_id_t dest = PARAM_COUNT;
    const mod_destination_address_t address =
        mod_destination_catalog_address_from_index(track, dest_index);
    if (address == MOD_DESTINATION_NONE)
    {
        out[0] = 'O';
        out[1] = 'f';
        out[2] = 'f';
        out[3] = '\0';
        return 1U;
    }

    if ((mod_destination_address_resolve(address, &target, &dest) == 0U)
            || (dest >= PARAM_COUNT))
    {
        return 0U;
    }

    const char *name = NULL;
    if (dest == PARAM_LFO1_RATE)
    {
        name = "lfo1rate";
    }
    else if (dest == PARAM_LFO2_RATE)
    {
        name = "lfo2rate";
    }
    else if (dest == PARAM_LFO3_RATE)
    {
        name = "lfo3rate";
    }
    else if ((name = mod_destination_prism_label_for_param(dest)) == NULL)
    {
        if (mod_destination_stack_label_for_track_param(target, dest, &name) == 0U)
        {
            name = mod_destination_wave_label_for_param(dest);
            if (name == NULL)
            {
                name = param_registry[dest].name;
            }
        }
    }
    if (name == NULL)
    {
        return 0U;
    }

    uint32_t prefix_len = 0U;
    entity_topology_descriptor_t owner;
    if ((entity_topology_get(track, &owner) != 0U)
            && (owner.role == ENTITY_ROLE_GROUP_MASTER))
    {
        const int written = (target == BRICK_ENTITY_GROUP_MASTER_ID)
            ? snprintf(out, out_len, "MASTER ")
            : snprintf(out, out_len, "SUB%u ",
                (unsigned int)(target - BRICK_ENTITY_FIRST_GROUP_CHILD_ID + 1U));
        if ((written < 0) || ((uint32_t)written >= out_len))
        {
            return 0U;
        }
        prefix_len = (uint32_t)written;
    }
    uint32_t name_index = 0U;
    uint32_t i = prefix_len;
    for (; (i + 1U) < out_len; ++i, ++name_index)
    {
        const char c = name[name_index];
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
        case PARAM_LFO1_RATE: return "L1Rt";
        case PARAM_LFO2_RATE: return "L2Rt";
        case PARAM_LFO3_RATE: return "L3Rt";
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
        case PARAM_STACK_OSC1_LEVEL: return "O1Lv";
        case PARAM_STACK_OSC2_LEVEL: return "O2Lv";
        case PARAM_STACK_OSC3_LEVEL: return "O3Lv";
        case PARAM_STACK_NOISE_LEVEL: return "Noiz";
        case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC3_TUNE: return "Tune";
        case PARAM_STACK_OSC1_TIMBRE:
        case PARAM_STACK_OSC2_TIMBRE:
        case PARAM_STACK_OSC3_TIMBRE: return "Timb";
        case PARAM_STACK_OSC1_COLOR:
        case PARAM_STACK_OSC2_COLOR:
        case PARAM_STACK_OSC3_COLOR: return "Col";
        case PARAM_PRISM_TIMBRE: return "O1P1";
        case PARAM_PRISM_COLOR: return "O1P2";
        case PARAM_PRISM_MODULATION: return "O1AM";
        case PARAM_PRISM_COARSE: return "O1Tn";
        case PARAM_PRISM_FM: return "O1FM";
        case PARAM_PRISM_LEVEL: return "O1Lv";
        case PARAM_PRISM_OSC2_TIMBRE: return "O2P1";
        case PARAM_PRISM_OSC2_COLOR: return "O2P2";
        case PARAM_PRISM_OSC2_MODULATION: return "O2AM";
        case PARAM_PRISM_OSC2_COARSE: return "O2Tn";
        case PARAM_PRISM_OSC2_FM: return "O2FM";
        case PARAM_PRISM_OSC2_LEVEL: return "O2Lv";
        case PARAM_WAVE_OSC1_POS: return "O1Ps";
        case PARAM_WAVE_OSC1_LEVEL: return "O1Lv";
        case PARAM_WAVE_OSC1_TUNE: return "O1Tn";
        case PARAM_WAVE_OSC2_POS: return "O2Ps";
        case PARAM_WAVE_OSC2_LEVEL: return "O2Lv";
        case PARAM_WAVE_OSC2_TUNE: return "O2Tn";
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

    uint8_t target = track;
    param_id_t dest = PARAM_COUNT;
    const mod_destination_address_t address =
        mod_destination_catalog_address_from_index(track, dest_index);
    if (address == MOD_DESTINATION_NONE)
    {
        mod_destination_copy_short_label("Off", out, out_len);
        return 1U;
    }

    if ((mod_destination_address_resolve(address, &target, &dest) == 0U)
            || (dest >= PARAM_COUNT))
    {
        return 0U;
    }

    const char *label = NULL;
    label = mod_destination_short_label_for_param(dest);
    if (label == NULL)
    {
        if (mod_destination_stack_label_for_track_param(target, dest, &label) == 0U)
        {
            (void)param_prism_label_for_track_param(target, dest, &label);
        }
    }
    if (label == NULL)
    {
        label = param_registry[dest].name;
    }
    if (label == NULL)
    {
        return 0U;
    }

    entity_topology_descriptor_t owner;
    if ((entity_topology_get(track, &owner) != 0U)
            && (owner.role == ENTITY_ROLE_GROUP_MASTER)
            && (out_len >= 5U))
    {
        out[0] = (target == BRICK_ENTITY_GROUP_MASTER_ID)
            ? 'M' : (char)('1' + target - BRICK_ENTITY_FIRST_GROUP_CHILD_ID);
        out[1] = ':';
        out[2] = label[0];
        out[3] = (label[1] != '\0') ? label[1] : '\0';
        out[4] = '\0';
        return 1U;
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
