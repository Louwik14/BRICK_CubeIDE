#include "Mod/mod_destination_catalog.h"

#include <stdio.h>
#include "Storage/memory_layout.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_fx_runtime.h"
#include "Audio/fx_audio_drift.h"

#include "Audio/audio_xfade.h"
#include "Audio/drum_synth.h"
#include "Audio/md_model.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_fm_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/synth_polyphony.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/track_runtime.h"
#include "Core/entity_topology.h"
#include "Core/track_tone_sound_state.h"
#include "Core/track_sound_state.h"
#include "Param/param_filter.h"
#include "Param/audio_fx_param_catalog.h"
#include "Param/param_registry.h"
#include "Param/param_registry_backends.h"
#include "Param/param_prism_labels.h"
#include "Param/param_stack_labels.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_env3.h"
#include "Seq/seq_types.h"
#include "mixer.h"
#include "midi.h"

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
    uint8_t audio_fx_model_a;
    uint8_t audio_fx_model_b;
    uint8_t prism_model[2];
    uint8_t stack_model[BRICK6_STACK_SLOT_COUNT];
    uint8_t drum_md_slot_count;
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
    uint8_t drum_md_slot_count;
} mod_destination_context_view_t;

typedef struct
{
    uint8_t valid;
    uint8_t value;
} mod_destination_midi_cc_cache_t;

CONTROL_M4_SRAM2 static mod_destination_cache_t g_mod_destination_cache[SEQ_TRACK_COUNT];
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
        case PARAM_MIX_SEND3:
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
        case PARAM_SAMPLER_LENGTH:
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
        case PARAM_PRISM_TUNE:
        case PARAM_PRISM_PITCH_MOD1:
        case PARAM_PRISM_OSC1_PARAM1:
        case PARAM_PRISM_OSC1_AMOD:
        case PARAM_PRISM_OSC1_PARAM2:
        case PARAM_PRISM_BALANCE:
        case PARAM_PRISM_DETUNE:
        case PARAM_PRISM_DRIFT:
        case PARAM_PRISM_PITCH_MOD2:
        case PARAM_PRISM_OSC2_PARAM1:
        case PARAM_PRISM_OSC2_AMOD:
        case PARAM_PRISM_OSC2_PARAM2:
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
        case PARAM_WAVE_OSC2_POS:
        case PARAM_WAVE_OSC1_START:
        case PARAM_WAVE_OSC2_START:
        case PARAM_WAVE_OSC1_LEN:
        case PARAM_WAVE_OSC2_LEN:
        case PARAM_WAVE_VOLUME:
        case PARAM_WAVE_BALANCE:
        case PARAM_WAVE_TUNE:
        case PARAM_WAVE_DETUNE:
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
        case PARAM_MIX_SEND3:
            mixer_set_track_send_level(ctx->audio_binding.mix_track_id, 2U, mod_destination_clampf(value, 0.0f, 1.0f));
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
        case PARAM_SAMPLER_LENGTH:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_RAM) { return 0U; }
            brick6_sampler_runtime_set_length(track, mod_destination_clampf(value, 0.0f, 1.0f));
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
        case PARAM_PRISM_TUNE:
            brick6_braids_runtime_set_tune(ctx->audio_binding.instance_id, mod_destination_clampf(value, -60.0f, 60.0f));
            return 1U;
        case PARAM_PRISM_DETUNE:
            brick6_braids_runtime_set_detune(ctx->audio_binding.instance_id, mod_destination_clampf(value, -24.0f, 24.0f));
            return 1U;
        case PARAM_PRISM_DRIFT:
            brick6_braids_runtime_set_drift(ctx->audio_binding.instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_PITCH_MOD1:
            brick6_braids_runtime_set_osc_pitch_mod(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC1_PARAM1:
            brick6_braids_runtime_set_osc_timbre(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC1_AMOD:
            brick6_braids_runtime_set_osc_modulation(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC1_PARAM2:
            brick6_braids_runtime_set_osc_color(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_BALANCE:
            brick6_braids_runtime_set_balance(ctx->audio_binding.instance_id, mod_destination_clampf(value, -1.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_PITCH_MOD2:
            brick6_braids_runtime_set_osc_pitch_mod(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_PARAM1:
            brick6_braids_runtime_set_osc_timbre(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_AMOD:
            brick6_braids_runtime_set_osc_modulation(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_PARAM2:
            brick6_braids_runtime_set_osc_color(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
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
        case PARAM_WAVE_OSC2_POS:
            brick6_wave_runtime_set_osc_pos(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC1_START:
            brick6_wave_runtime_set_osc_start(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC2_START:
            brick6_wave_runtime_set_osc_start(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC1_LEN:
            brick6_wave_runtime_set_osc_len(ctx->audio_binding.instance_id, 0U, mod_destination_clampf(value, 0.01f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC2_LEN:
            brick6_wave_runtime_set_osc_len(ctx->audio_binding.instance_id, 1U, mod_destination_clampf(value, 0.01f, 1.0f));
            return 1U;
        case PARAM_WAVE_VOLUME:
            brick6_wave_runtime_set_volume(ctx->audio_binding.instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_BALANCE:
            brick6_wave_runtime_set_balance(ctx->audio_binding.instance_id, mod_destination_clampf(value, -1.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_TUNE:
            brick6_wave_runtime_set_tune(ctx->audio_binding.instance_id, mod_destination_clampf(value, -60.0f, 60.0f));
            return 1U;
        case PARAM_WAVE_DETUNE:
            brick6_wave_runtime_set_detune(ctx->audio_binding.instance_id, mod_destination_clampf(value, -24.0f, 24.0f));
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

    if (param_backend_send_midi_cc_audio(ctx, dest, (float)cc_value) == 0U)
    {
        return 0U;
    }

    cache->valid = 1U;
    cache->value = cc_value;
    return 1U;
}

static uint8_t mod_destination_is_continuous_rampable(param_id_t dest);
static uint8_t mod_destination_is_segment_rate(param_id_t dest);

static uint8_t mod_destination_prepared_opcode(param_id_t dest,
                                               uint8_t *out_opcode,
                                               uint8_t *out_subindex)
{
    uint8_t opcode = MOD_DEST_APPLY_GENERIC;
    uint8_t subindex = 0U;
    switch (dest)
    {
        case PARAM_LFO1_RATE: case PARAM_LFO2_RATE: case PARAM_LFO3_RATE:
            opcode = MOD_DEST_APPLY_LFO_RATE;
            subindex = (uint8_t)(dest - PARAM_LFO1_RATE) / (uint8_t)MOD_LFO_PARAM_COUNT;
            break;
        case PARAM_MIX_LEVEL: opcode = MOD_DEST_APPLY_MIX_LEVEL; break;
        case PARAM_MIX_PAN: opcode = MOD_DEST_APPLY_MIX_PAN; break;
        case PARAM_MIX_SEND1: case PARAM_MIX_SEND2: case PARAM_MIX_SEND3:
            opcode = MOD_DEST_APPLY_MIX_SEND;
            subindex = (uint8_t)(dest - PARAM_MIX_SEND1);
            break;
        case PARAM_AUDIO_FX_P1:
        case PARAM_AUDIO_FX_B_P1:
        case PARAM_GROUP_FX_A_LEVEL:
        case PARAM_GROUP_FX_B_LEVEL:
            opcode = MOD_DEST_APPLY_AUDIO_FX_DELAY;
            subindex = (dest == PARAM_AUDIO_FX_B_P1) ? 1U : 0U;
            break;
        case PARAM_FILTER_CUTOFF: opcode = MOD_DEST_APPLY_FILTER_CUTOFF; break;
        case PARAM_FILTER_RESONANCE: opcode = MOD_DEST_APPLY_FILTER_RESONANCE; break;
        case PARAM_FILTER_EG_AMT: opcode = MOD_DEST_APPLY_FILTER_EG_AMOUNT; break;
        case PARAM_FILTER_ATTACK: opcode = MOD_DEST_APPLY_FILTER_ATTACK; break;
        case PARAM_FILTER_DECAY: opcode = MOD_DEST_APPLY_FILTER_DECAY; break;
        case PARAM_FILTER_SUSTAIN: opcode = MOD_DEST_APPLY_FILTER_SUSTAIN; break;
        case PARAM_FILTER_RELEASE: opcode = MOD_DEST_APPLY_FILTER_RELEASE; break;
        case PARAM_VCA_ATTACK: opcode = MOD_DEST_APPLY_VCA_ATTACK; break;
        case PARAM_VCA_DECAY: opcode = MOD_DEST_APPLY_VCA_DECAY; break;
        case PARAM_VCA_SUSTAIN: opcode = MOD_DEST_APPLY_VCA_SUSTAIN; break;
        case PARAM_VCA_RELEASE: opcode = MOD_DEST_APPLY_VCA_RELEASE; break;
        case PARAM_ENV3_ATTACK: case PARAM_ENV3_DECAY:
        case PARAM_ENV3_SUSTAIN: case PARAM_ENV3_RELEASE:
            opcode = MOD_DEST_APPLY_ENV3;
            subindex = (uint8_t)(dest - PARAM_ENV3_ATTACK);
            break;
        case PARAM_SAMPLER_GAIN: opcode = MOD_DEST_APPLY_SAMPLER_GAIN; break;
        case PARAM_SAMPLER_START: opcode = MOD_DEST_APPLY_SAMPLER_START; break;
        case PARAM_SAMPLER_LENGTH: opcode = MOD_DEST_APPLY_SAMPLER_LENGTH; break;
        case PARAM_SAMPLER_LOOP_START: opcode = MOD_DEST_APPLY_SAMPLER_LOOP_START; break;
        case PARAM_SAMPLER_TUNE: opcode = MOD_DEST_APPLY_SAMPLER_TUNE; break;
        case PARAM_LOOPER_XFADE: opcode = MOD_DEST_APPLY_LOOPER_XFADE; break;
        case PARAM_PRISM_TUNE: opcode = MOD_DEST_APPLY_PRISM_TUNE; break;
        case PARAM_PRISM_DETUNE: opcode = MOD_DEST_APPLY_PRISM_DETUNE; break;
        case PARAM_PRISM_DRIFT: opcode = MOD_DEST_APPLY_PRISM_DRIFT; break;
        case PARAM_PRISM_BALANCE: opcode = MOD_DEST_APPLY_PRISM_BALANCE; break;
        case PARAM_PRISM_PITCH_MOD1: case PARAM_PRISM_PITCH_MOD2:
            opcode = MOD_DEST_APPLY_PRISM_PITCH_MOD; subindex = (dest == PARAM_PRISM_PITCH_MOD2); break;
        case PARAM_PRISM_OSC1_PARAM1: case PARAM_PRISM_OSC2_PARAM1:
            opcode = MOD_DEST_APPLY_PRISM_TIMBRE; subindex = (dest == PARAM_PRISM_OSC2_PARAM1); break;
        case PARAM_PRISM_OSC1_AMOD: case PARAM_PRISM_OSC2_AMOD:
            opcode = MOD_DEST_APPLY_PRISM_MODULATION; subindex = (dest == PARAM_PRISM_OSC2_AMOD); break;
        case PARAM_PRISM_OSC1_PARAM2: case PARAM_PRISM_OSC2_PARAM2:
            opcode = MOD_DEST_APPLY_PRISM_COLOR; subindex = (dest == PARAM_PRISM_OSC2_PARAM2); break;
        case PARAM_FM_RATIO: opcode = MOD_DEST_APPLY_FM_RATIO; break;
        case PARAM_FM_BRIGHT: opcode = MOD_DEST_APPLY_FM_BRIGHT; break;
        case PARAM_FM_BODY: opcode = MOD_DEST_APPLY_FM_BODY; break;
        case PARAM_FM_DETAIL: opcode = MOD_DEST_APPLY_FM_DETAIL; break;
        case PARAM_FM_METAL: opcode = MOD_DEST_APPLY_FM_METAL; break;
        case PARAM_FM_ENV_ATTACK: case PARAM_FM_ENV_DECAY:
        case PARAM_FM_ENV_SUSTAIN: case PARAM_FM_ENV_RELEASE:
            opcode = MOD_DEST_APPLY_FM_ENV;
            subindex = (uint8_t)(dest - PARAM_FM_ENV_ATTACK);
            break;
        case PARAM_STACK_NOISE_LEVEL: opcode = MOD_DEST_APPLY_STACK_NOISE; break;
        case PARAM_WAVE_OSC1_POS: case PARAM_WAVE_OSC2_POS:
            opcode = MOD_DEST_APPLY_WAVE_POSITION; subindex = (dest == PARAM_WAVE_OSC2_POS); break;
        case PARAM_WAVE_OSC1_START: case PARAM_WAVE_OSC2_START:
            opcode = MOD_DEST_APPLY_WAVE_START; subindex = (dest == PARAM_WAVE_OSC2_START); break;
        case PARAM_WAVE_OSC1_LEN: case PARAM_WAVE_OSC2_LEN:
            opcode = MOD_DEST_APPLY_WAVE_LENGTH; subindex = (dest == PARAM_WAVE_OSC2_LEN); break;
        case PARAM_WAVE_VOLUME: opcode = MOD_DEST_APPLY_WAVE_VOLUME; break;
        case PARAM_WAVE_BALANCE: opcode = MOD_DEST_APPLY_WAVE_BALANCE; break;
        case PARAM_WAVE_TUNE: opcode = MOD_DEST_APPLY_WAVE_TUNE; break;
        case PARAM_WAVE_DETUNE: opcode = MOD_DEST_APPLY_WAVE_DETUNE; break;
        default:
            if (mod_destination_is_direct_midi_cc(dest) != 0U)
                opcode = MOD_DEST_APPLY_MIDI_CC;
            else if (mod_destination_is_direct_drum(dest) != 0U)
                opcode = MOD_DEST_APPLY_DRUM_PARAM;
            else if (mod_destination_is_direct_stack(dest) != 0U)
            {
                uint8_t slot_param = 0U;
                if (mod_destination_stack_slot_for_id(dest, &subindex, &slot_param) == 0U)
                    return 0U;
                if (slot_param == 0U) opcode = MOD_DEST_APPLY_STACK_LEVEL;
                else if (slot_param == 2U) opcode = MOD_DEST_APPLY_STACK_TUNE;
                else if (slot_param == 3U) opcode = MOD_DEST_APPLY_STACK_TIMBRE;
                else if (slot_param == 4U) opcode = MOD_DEST_APPLY_STACK_COLOR;
                else return 0U;
            }
            break;
    }
    *out_opcode = opcode;
    *out_subindex = subindex;
    return 1U;
}

uint8_t mod_destination_catalog_prepare(uint8_t target,
                                        param_id_t dest,
                                        const track_audio_runtime_ctx_t *ctx,
                                        mod_destination_prepared_t *out)
{
    if ((target >= SEQ_TRACK_COUNT) || (dest >= PARAM_COUNT)
            || (ctx == NULL) || (out == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND))
        return 0U;

    mod_destination_prepared_t prepared = {
        .param = (uint16_t)dest,
        .target = target,
        .endpoint = ctx->audio_binding.instance_id,
        .aux = ctx->audio_binding.engine
    };
    if (mod_destination_prepared_opcode(dest, &prepared.opcode,
                                        &prepared.subindex) == 0U)
        return 0U;
    if (prepared.opcode == MOD_DEST_APPLY_AUDIO_FX_DELAY)
    {
        const track_sound_state_t *const state = track_sound_state_get_const(target);
        const uint8_t model = (state == NULL) ? AUDIO_FX_MODEL_OFF
            : (prepared.subindex != 0U) ? state->audio_fx_b_model
                                       : state->audio_fx_model;
        if (model != AUDIO_FX_MODEL_DRIFT)
            prepared.opcode = MOD_DEST_APPLY_GENERIC;
    }
    if ((prepared.opcode >= MOD_DEST_APPLY_MIX_LEVEL)
            && (prepared.opcode <= MOD_DEST_APPLY_VCA_RELEASE))
        prepared.endpoint = ctx->audio_binding.mix_track_id;
    if (prepared.opcode == MOD_DEST_APPLY_VCA_RELEASE)
        prepared.subindex = ctx->audio_binding.instance_id;
    if (prepared.opcode == MOD_DEST_APPLY_SAMPLER_GAIN)
        prepared.aux = ctx->type;
    else if (prepared.opcode == MOD_DEST_APPLY_MIDI_CC)
    {
        uint8_t channel = 0U;
        uint8_t cache_index = 0U;
        if ((audio_note_engine_adapter_audio_midi_channel_zero_based(ctx, &channel) == 0U)
                || (mod_destination_midi_cc_cache_index(dest, &cache_index) == 0U))
            return 0U;
        prepared.endpoint = channel;
        prepared.subindex = cache_index;
        prepared.aux = param_backend_midi_cc_number_from_id(dest);
    }
    else if (prepared.opcode == MOD_DEST_APPLY_DRUM_PARAM)
    {
        prepared.aux = ((dest >= PARAM_DRUM_MD_P1) && (dest <= PARAM_DRUM_MD_P8)) ? 0U
            : (dest == PARAM_DRUM_TRX_BD_PITCH) ? 1U
            : (dest == PARAM_DRUM_TRX_BD_DECAY) ? 2U : 3U;
    }
    if (mod_destination_is_continuous_rampable(dest) != 0U)
        prepared.flags |= MOD_DEST_PREPARED_RAMP_CONTINUOUS;
    if (mod_destination_is_segment_rate(dest) != 0U)
        prepared.flags |= MOD_DEST_PREPARED_RAMP_SEGMENT;
    *out = prepared;
    return 1U;
}

static float mod_destination_fm_macro_unit(float value)
{
    return 0.5f + 0.5f * mod_destination_clampf(value, -1.0f, 1.0f);
}

uint8_t mod_destination_catalog_apply_prepared(
    const mod_destination_prepared_t *p, float value)
{
    if ((p == NULL) || (p->opcode == MOD_DEST_APPLY_NONE)) return 0U;
    switch ((mod_destination_apply_opcode_t)p->opcode)
    {
        case MOD_DEST_APPLY_LFO_RATE: return mod_lfo_v1_apply_track_param_temp(p->target, p->subindex, MOD_LFO_PARAM_RATE, value);
        case MOD_DEST_APPLY_MIX_LEVEL: mixer_set_track_gain(p->endpoint, mod_destination_clampf(value, 0.0f, 2.0f)); return 1U;
        case MOD_DEST_APPLY_MIX_PAN: mixer_set_track_pan(p->endpoint, mod_destination_clampf(value, -1.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_MIX_SEND: mixer_set_track_send_level(p->endpoint, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_FILTER_CUTOFF: mixer_set_track_filter_cutoff_modulated(p->endpoint, param_filter_ui127_to_cutoff_hz(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_RESONANCE: mixer_set_track_filter_resonance(p->endpoint, param_filter_ui127_to_resonance(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_EG_AMOUNT: mixer_set_track_filter_eg_amount(p->endpoint, param_filter_ui127_to_eg_amount(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_ATTACK: mixer_set_track_filter_attack(p->endpoint, param_filter_ui127_to_attack_s(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_DECAY: mixer_set_track_filter_decay(p->endpoint, param_filter_ui127_to_decay_s(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_SUSTAIN: mixer_set_track_filter_sustain(p->endpoint, param_filter_ui127_to_sustain(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_RELEASE: mixer_set_track_filter_release(p->endpoint, param_filter_ui127_to_release_s(value)); return 1U;
        case MOD_DEST_APPLY_VCA_ATTACK: mixer_set_track_vca_attack(p->endpoint, param_filter_ui127_to_attack_s(value)); return 1U;
        case MOD_DEST_APPLY_VCA_DECAY: mixer_set_track_vca_decay(p->endpoint, param_filter_ui127_to_decay_s(value)); return 1U;
        case MOD_DEST_APPLY_VCA_SUSTAIN: mixer_set_track_vca_sustain(p->endpoint, param_filter_ui127_to_sustain(value)); return 1U;
        case MOD_DEST_APPLY_VCA_RELEASE:
        {
            const float seconds = param_filter_ui127_to_release_s(value);
            mixer_set_track_vca_release(p->endpoint, seconds);
            if (p->aux == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
                brick6_braids_runtime_set_vca_release_seconds(p->subindex, seconds);
            return 1U;
        }
        case MOD_DEST_APPLY_ENV3: return mod_env3_apply_track_param_temp(
            p->target, (mod_env3_param_t)p->subindex, value);
        case MOD_DEST_APPLY_SAMPLER_GAIN:
            if (p->aux == (uint8_t)TRACK_RUNTIME_TYPE_MULTI) brick6_sampler_runtime_set_multi_gain(p->target, mod_destination_clampf(value, 0.0f, 2.0f));
            else brick6_sampler_runtime_set_gain(p->target, mod_destination_clampf(value, 0.0f, 2.0f));
            return 1U;
        case MOD_DEST_APPLY_SAMPLER_START: brick6_sampler_runtime_set_start(p->target, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_SAMPLER_LENGTH: brick6_sampler_runtime_set_length(p->target, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_SAMPLER_LOOP_START: brick6_sampler_runtime_set_loop_start(p->target, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_SAMPLER_TUNE: brick6_sampler_runtime_set_tune(p->target, mod_destination_clampf(value, -24.0f, 24.0f)); return 1U;
        case MOD_DEST_APPLY_LOOPER_XFADE: audio_xfade_set(mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_TUNE: brick6_braids_runtime_set_tune(p->endpoint, mod_destination_clampf(value, -60.0f, 60.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_DETUNE: brick6_braids_runtime_set_detune(p->endpoint, mod_destination_clampf(value, -24.0f, 24.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_DRIFT: brick6_braids_runtime_set_drift(p->endpoint, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_PITCH_MOD: brick6_braids_runtime_set_osc_pitch_mod(p->endpoint, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_TIMBRE: brick6_braids_runtime_set_osc_timbre(p->endpoint, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_MODULATION: brick6_braids_runtime_set_osc_modulation(p->endpoint, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_COLOR: brick6_braids_runtime_set_osc_color(p->endpoint, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_BALANCE: brick6_braids_runtime_set_balance(p->endpoint, mod_destination_clampf(value, -1.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_FM_RATIO: brick6_fm_runtime_set_ratio(p->endpoint, mod_destination_fm_macro_unit(value)); return 1U;
        case MOD_DEST_APPLY_FM_BRIGHT: brick6_fm_runtime_set_bright(p->endpoint, mod_destination_fm_macro_unit(value)); return 1U;
        case MOD_DEST_APPLY_FM_BODY: brick6_fm_runtime_set_body(p->endpoint, mod_destination_fm_macro_unit(value)); return 1U;
        case MOD_DEST_APPLY_FM_DETAIL: brick6_fm_runtime_set_detail(p->endpoint, mod_destination_fm_macro_unit(value)); return 1U;
        case MOD_DEST_APPLY_FM_METAL: brick6_fm_runtime_set_metal(p->endpoint, mod_destination_fm_macro_unit(value)); return 1U;
        case MOD_DEST_APPLY_FM_ENV:
        {
            track_tone_fm_macros_t m;
            if (brick6_fm_runtime_get_macros(p->endpoint, &m) == 0U) return 0U;
            float a=m.env_attack, d=m.env_decay, s=m.env_sustain, r=m.env_release;
            if (p->subindex == 0U) a=value; else if (p->subindex == 1U) d=value;
            else if (p->subindex == 2U) s=value; else r=value;
            brick6_fm_runtime_set_env(p->endpoint, mod_destination_fm_macro_unit(a), mod_destination_fm_macro_unit(d), mod_destination_fm_macro_unit(s), mod_destination_fm_macro_unit(r));
            return 1U;
        }
        case MOD_DEST_APPLY_STACK_LEVEL: brick6_stack_runtime_set_slot_level(p->endpoint, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_STACK_TUNE: brick6_stack_runtime_set_slot_tune(p->endpoint, p->subindex, mod_destination_clampf(value, -24.0f, 24.0f)); return 1U;
        case MOD_DEST_APPLY_STACK_TIMBRE: brick6_stack_runtime_set_slot_timbre(p->endpoint, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_STACK_COLOR: brick6_stack_runtime_set_slot_color(p->endpoint, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_STACK_NOISE: brick6_stack_runtime_set_noise_level(p->endpoint, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_POSITION: brick6_wave_runtime_set_osc_pos(p->endpoint, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_START: brick6_wave_runtime_set_osc_start(p->endpoint, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_LENGTH: brick6_wave_runtime_set_osc_len(p->endpoint, p->subindex, mod_destination_clampf(value, 0.01f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_VOLUME: brick6_wave_runtime_set_volume(p->endpoint, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_BALANCE: brick6_wave_runtime_set_balance(p->endpoint, mod_destination_clampf(value, -1.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_TUNE: brick6_wave_runtime_set_tune(p->endpoint, mod_destination_clampf(value, -60.0f, 60.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_DETUNE: brick6_wave_runtime_set_detune(p->endpoint, mod_destination_clampf(value, -24.0f, 24.0f)); return 1U;
        case MOD_DEST_APPLY_DRUM_PARAM:
        {
            const float v = (p->aux == 0U) ? mod_destination_clampf(value, 0.0f, 127.0f)
                : (p->aux == 1U) ? mod_destination_clampf(value, -48.0f, 24.0f)
                : (p->aux == 2U) ? mod_destination_clampf(value, 0.01f, 2.0f)
                : mod_destination_clampf(value, 0.0f, 1.0f);
            return drum_synth_set_param_for_instance(p->endpoint, (param_id_t)p->param, v);
        }
        case MOD_DEST_APPLY_MIDI_CC:
        {
            const uint8_t v = (uint8_t)(mod_destination_clampf(value, 0.0f, 127.0f) + 0.5f);
            mod_destination_midi_cc_cache_t *const cache = &g_mod_destination_midi_cc_cache[p->target][p->subindex];
            if ((cache->valid != 0U) && (cache->value == v)) return 1U;
            midi_cc(MIDI_DEST_BOTH, p->endpoint, p->aux, v);
            cache->valid = 1U; cache->value = v; return 1U;
        }
        case MOD_DEST_APPLY_AUDIO_FX_DELAY:
            return audio_fx_runtime_apply_drift_delay_modulated(
                (brick_entity_id_t)p->target,
                (p->subindex != 0U) ? PARAM_AUDIO_FX_B_P1 : PARAM_AUDIO_FX_P1,
                mod_destination_clampf(value,0.0f,FX_AUDIO_DRIFT_DELAY_MOD_MAX_CONTROL));
        case MOD_DEST_APPLY_GENERIC: return param_registry_apply_track_value_rt_fast((param_id_t)p->param, p->target, value);
        default: return 0U;
    }
}

uint8_t mod_destination_catalog_apply_ramp_prepared(
    const mod_destination_prepared_t *p, const mod_destination_ramp_t *ramp)
{
    if ((p == NULL) || (ramp == NULL)) return 0U;
    if ((ramp->discontinuous == 0U) && (ramp->frames > 1U)
            && ((p->flags & MOD_DEST_PREPARED_RAMP_SEGMENT) != 0U))
        return mod_destination_catalog_apply_prepared(p, ramp->end);
    const uint8_t applied = mod_destination_catalog_apply_prepared(p, ramp->current);
    if ((applied != 0U) && (ramp->discontinuous == 0U) && (ramp->frames > 1U)
            && ((p->flags & MOD_DEST_PREPARED_RAMP_CONTINUOUS) != 0U))
        return mod_destination_catalog_apply_prepared(p, ramp->end);
    return applied;
}

uint8_t mod_destination_catalog_apply_poly_prepared(
    const mod_destination_prepared_t *p, uint8_t voice_slot, float value)
{
    if (p == NULL) return 0U;
    struct multi_voice_dsp_slot_t *multi_slot = NULL;
    const uint8_t sampler_multi =
        (p->aux == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER) ? 1U : 0U;
    if (sampler_multi != 0U)
    {
        if (voice_slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) return 0U;
        multi_slot = brick6_sampler_runtime_get_multi_voice_dsp(
            (uint8_t)(voice_slot - SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET));
        if (multi_slot == NULL) return 0U;
    }
    switch ((mod_destination_apply_opcode_t)p->opcode)
    {
        case MOD_DEST_APPLY_FILTER_CUTOFF:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_cutoff(multi_slot, param_filter_ui127_to_cutoff_hz(value));
            else mixer_poly_voice_set_cutoff(voice_slot, param_filter_ui127_to_cutoff_hz(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_RESONANCE:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_resonance(multi_slot, param_filter_ui127_to_resonance(value));
            else mixer_poly_voice_set_resonance(voice_slot, param_filter_ui127_to_resonance(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_EG_AMOUNT:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_eg_amount(multi_slot, param_filter_ui127_to_eg_amount(value));
            else mixer_poly_voice_set_eg_amount(voice_slot, param_filter_ui127_to_eg_amount(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_ATTACK:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_env_attack(multi_slot, param_filter_ui127_to_attack_s(value));
            else mixer_poly_voice_set_filter_attack(voice_slot, param_filter_ui127_to_attack_s(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_DECAY:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_env_decay(multi_slot, param_filter_ui127_to_decay_s(value));
            else mixer_poly_voice_set_filter_decay(voice_slot, param_filter_ui127_to_decay_s(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_SUSTAIN:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_env_sustain(multi_slot, param_filter_ui127_to_sustain(value));
            else mixer_poly_voice_set_filter_sustain(voice_slot, param_filter_ui127_to_sustain(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_RELEASE:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_env_release(multi_slot, param_filter_ui127_to_release_s(value));
            else mixer_poly_voice_set_filter_release(voice_slot, param_filter_ui127_to_release_s(value));
            return 1U;
        case MOD_DEST_APPLY_VCA_ATTACK:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_vca_attack(multi_slot, param_filter_ui127_to_attack_s(value));
            else mixer_poly_voice_set_vca_attack(voice_slot, param_filter_ui127_to_attack_s(value));
            return 1U;
        case MOD_DEST_APPLY_VCA_DECAY:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_vca_decay(multi_slot, param_filter_ui127_to_decay_s(value));
            else mixer_poly_voice_set_vca_decay(voice_slot, param_filter_ui127_to_decay_s(value));
            return 1U;
        case MOD_DEST_APPLY_VCA_SUSTAIN:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_vca_sustain(multi_slot, param_filter_ui127_to_sustain(value));
            else mixer_poly_voice_set_vca_sustain(voice_slot, param_filter_ui127_to_sustain(value));
            return 1U;
        case MOD_DEST_APPLY_VCA_RELEASE:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_vca_release(multi_slot, param_filter_ui127_to_release_s(value));
            else mixer_poly_voice_set_vca_release(voice_slot, param_filter_ui127_to_release_s(value));
            return 1U;
        case MOD_DEST_APPLY_PRISM_TUNE: brick6_braids_runtime_set_tune(voice_slot, mod_destination_clampf(value, -60.0f, 60.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_DETUNE: brick6_braids_runtime_set_detune(voice_slot, mod_destination_clampf(value, -24.0f, 24.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_DRIFT: brick6_braids_runtime_set_drift(voice_slot, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_PITCH_MOD: brick6_braids_runtime_set_osc_pitch_mod(voice_slot, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_TIMBRE: brick6_braids_runtime_set_osc_timbre(voice_slot, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_MODULATION: brick6_braids_runtime_set_osc_modulation(voice_slot, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_COLOR: brick6_braids_runtime_set_osc_color(voice_slot, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_PRISM_BALANCE: brick6_braids_runtime_set_balance(voice_slot, mod_destination_clampf(value, -1.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_STACK_LEVEL: brick6_stack_runtime_set_slot_level(voice_slot, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_STACK_TUNE: brick6_stack_runtime_set_slot_tune(voice_slot, p->subindex, mod_destination_clampf(value, -24.0f, 24.0f)); return 1U;
        case MOD_DEST_APPLY_STACK_TIMBRE: brick6_stack_runtime_set_slot_timbre(voice_slot, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_STACK_COLOR: brick6_stack_runtime_set_slot_color(voice_slot, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_STACK_NOISE: brick6_stack_runtime_set_noise_level(voice_slot, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_POSITION: brick6_wave_runtime_set_osc_pos(voice_slot, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_START: brick6_wave_runtime_set_osc_start(voice_slot, p->subindex, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_LENGTH: brick6_wave_runtime_set_osc_len(voice_slot, p->subindex, mod_destination_clampf(value, 0.01f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_VOLUME: brick6_wave_runtime_set_volume(voice_slot, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_BALANCE: brick6_wave_runtime_set_balance(voice_slot, mod_destination_clampf(value, -1.0f, 1.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_TUNE: brick6_wave_runtime_set_tune(voice_slot, mod_destination_clampf(value, -60.0f, 60.0f)); return 1U;
        case MOD_DEST_APPLY_WAVE_DETUNE: brick6_wave_runtime_set_detune(voice_slot, mod_destination_clampf(value, -24.0f, 24.0f)); return 1U;
        default: return 0U;
    }
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
        case PARAM_MIX_SEND3:
        case PARAM_AUDIO_FX_P1:
        case PARAM_AUDIO_FX_B_P1:
        case PARAM_FILTER_CUTOFF:
        case PARAM_FILTER_RESONANCE:
        case PARAM_SAMPLER_GAIN:
        case PARAM_SAMPLER_TUNE:
        case PARAM_PRISM_TUNE:
        case PARAM_PRISM_BALANCE:
        case PARAM_PRISM_DETUNE:
        case PARAM_PRISM_DRIFT:
        case PARAM_STACK_OSC1_LEVEL:
        case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC2_LEVEL:
        case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC3_LEVEL:
        case PARAM_STACK_OSC3_TUNE:
        case PARAM_WAVE_OSC1_POS:
        case PARAM_WAVE_OSC2_POS:
        case PARAM_WAVE_OSC1_START:
        case PARAM_WAVE_OSC2_START:
        case PARAM_WAVE_OSC1_LEN:
        case PARAM_WAVE_OSC2_LEN:
        case PARAM_WAVE_VOLUME:
        case PARAM_WAVE_BALANCE:
        case PARAM_WAVE_TUNE:
        case PARAM_WAVE_DETUNE:
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
        case PARAM_PRISM_TUNE:
        case PARAM_PRISM_DETUNE:
        case PARAM_PRISM_DRIFT:
        case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC3_TUNE:
        case PARAM_WAVE_TUNE:
        case PARAM_WAVE_DETUNE:
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
        if (((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_STACK)
                && (param_stack_dynamic_param_info(dest, NULL, NULL) != 0U)
                && (param_stack_param_is_active(track, dest) == 0U))
        {
            return 0U;
        }
        if (((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_PRISM)
                && (param_prism_param_is_active(track, dest) == 0U))
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
            if (ctx->drum_md_slot_count != 0U)
                return (uint8_t)((uint8_t)(dest - PARAM_DRUM_MD_P1)
                                  < ctx->drum_md_slot_count);
            return 0U;
        }
        if ((dest == PARAM_PRISM_OSC1_MODEL)
                || (dest == PARAM_PRISM_VOLUME)
                || (dest == PARAM_PRISM_PHASE1_RESET)
                || (dest == PARAM_PRISM_OSC2_MODEL)
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
        if ((dest == PARAM_FILTER_MORPH)
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

        return (((dest >= PARAM_FILTER_MORPH) && (dest <= PARAM_FILTER_ENVDLY))
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
                || (dest == PARAM_MIX_SEND2)
                || (dest == PARAM_MIX_SEND3)) ? 1U : 0U;
    }

    if (domain == TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX)
    {
        if (audio_fx_runtime_is_group_level_param(dest) != 0U)
        {
            entity_topology_descriptor_t topology;
            return (uint8_t)((ctx != NULL)
                && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (entity_topology_get((brick_entity_id_t)track, &topology) != 0U)
                && (topology.active != 0U)
                && (topology.role == ENTITY_ROLE_GROUP_CHILD));
        }
        const track_sound_state_t *const state = track_sound_state_get_const(track);
        uint8_t slot = 0U;
        uint8_t param_index = 0U;
        const uint8_t is_fx_param = audio_fx_param_catalog_param_info(
            dest, &slot, &param_index);
        const uint8_t model = (state == NULL) ? AUDIO_FX_MODEL_OFF
            : (slot != 0U) ? state->audio_fx_b_model : state->audio_fx_model;
        const char *name = NULL;
        return ((ctx != NULL)
                && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (state != NULL)
                && (is_fx_param != 0U)
                && (audio_fx_param_catalog_resolve(
                    model, param_index, &name) != 0U)) ? 1U : 0U;
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
                    || (ctx->mix_track_id >= SEQ_TRACK_COUNT))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return TRACK_RUNTIME_PARAM_ALLOWED;
        case TRACK_RUNTIME_RESOURCE_AUDIO_FX:
            return ((ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                    && (ctx->audio_routable != 0U))
                ? TRACK_RUNTIME_PARAM_ALLOWED
                : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
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
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX))
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
        .supports_vca_gate = audio_note_engine_adapter_ctx_supports_vca_gate(ctx),
        .drum_md_slot_count = track_tone_sound_state_md_slot_count(track)
    };
    return mod_destination_catalog_supported_view(
        track, dest, family, type, &view);
}

uint8_t mod_destination_catalog_supported_audio(uint8_t track,
                                                param_id_t dest,
                                                ui_track_family_t family,
                                                ui_track_type_t type,
                                                const track_audio_runtime_ctx_t *ctx,
                                                uint8_t drum_md_slot_count)
{
    if (ctx == NULL)
    {
        return 0U;
    }
    const mod_destination_context_view_t view = {
        .bind_state = ctx->audio_binding.bind_state,
        .family = ctx->family,
        .type = ctx->type,
        .mix_track_id = ctx->audio_binding.mix_track_id,
        .audio_routable = audio_note_engine_adapter_ctx_is_audio_routable(ctx),
        .supports_vca_gate = audio_note_engine_adapter_ctx_supports_vca_gate(ctx),
        .drum_md_slot_count = drum_md_slot_count
    };
    return mod_destination_catalog_supported_view(
        track, dest, family, type, &view);
}

static uint8_t mod_destination_cache_matches_context(uint8_t track,
                                                     const mod_destination_cache_t *cache,
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
    const track_sound_state_t *const state = track_sound_state_get_const(track);
    const uint8_t audio_fx_model_a = (state != NULL)
        ? state->audio_fx_model : AUDIO_FX_MODEL_OFF;
    const uint8_t audio_fx_model_b = (state != NULL)
        ? state->audio_fx_b_model : AUDIO_FX_MODEL_OFF;
    const track_tone_sound_state_t *const tone =
        track_tone_sound_state_get_const(track);

    return ((cache->ui_family == (uint8_t)family)
            && (cache->ui_type == (uint8_t)type)
            && (cache->rt_bind_state == ctx_bind_state)
            && (cache->rt_family == ctx_family)
            && (cache->rt_type == ctx_type)
            && (cache->rt_mix_track_id == ctx_mix_track_id)
            && (cache->audio_fx_model_a == audio_fx_model_a)
            && (cache->audio_fx_model_b == audio_fx_model_b)
            && (cache->prism_model[0] == ((tone != NULL)
                ? (uint8_t)(tone->prism.model[0] + 0.5f) : 0xFFU))
            && (cache->prism_model[1] == ((tone != NULL)
                ? (uint8_t)(tone->prism.model[1] + 0.5f) : 0xFFU))
            && (cache->stack_model[0] == ((tone != NULL)
                ? (uint8_t)(tone->stack.model[0] + 0.5f) : 0xFFU))
            && (cache->stack_model[1] == ((tone != NULL)
                ? (uint8_t)(tone->stack.model[1] + 0.5f) : 0xFFU))
            && (cache->stack_model[2] == ((tone != NULL)
                ? (uint8_t)(tone->stack.model[2] + 0.5f) : 0xFFU))
            && (cache->drum_md_slot_count == ((tone != NULL)
                ? track_tone_sound_state_md_slot_count(track) : 0U))) ? 1U : 0U;
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
                || (snapshot.family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM))),
        .drum_md_slot_count = track_tone_sound_state_md_slot_count(track)
    };
    mod_destination_cache_t *const cache = &g_mod_destination_cache[track];

    if (mod_destination_cache_matches_context(
            track, cache, family, type, &snapshot) != 0U)
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
        const param_id_t raw_param = (param_id_t)raw;
        if (raw_param == PARAM_MIX_SEND3)
        {
            continue;
        }
        const param_id_t ordered[2] = {
            raw_param,
            (raw_param == PARAM_MIX_SEND2) ? PARAM_MIX_SEND3 : PARAM_COUNT
        };
        for (uint8_t candidate = 0U; candidate < 2U; ++candidate)
        {
            const param_id_t param = ordered[candidate];
            if ((param >= PARAM_COUNT)
                    || (mod_destination_catalog_supported_view(
                        track, param, family, type, &view) == 0U))
            {
                continue;
            }
            if (count <= (uint16_t)PARAM_COUNT)
            {
                cache->index_to_param[count] = param;
                cache->param_to_index[(uint16_t)param] = count;
            }
            ++count;
        }
    }

    cache->count = count;
    cache->ui_family = (uint8_t)family;
    cache->ui_type = (uint8_t)type;
    cache->rt_bind_state = snapshot.binding.bind_state;
    cache->rt_family = snapshot.family;
    cache->rt_type = snapshot.type;
    cache->rt_mix_track_id = snapshot.binding.mix_track_id;
    const track_sound_state_t *const state = track_sound_state_get_const(track);
    cache->audio_fx_model_a = (state != NULL)
        ? state->audio_fx_model : AUDIO_FX_MODEL_OFF;
    cache->audio_fx_model_b = (state != NULL)
        ? state->audio_fx_b_model : AUDIO_FX_MODEL_OFF;
    const track_tone_sound_state_t *const tone =
        track_tone_sound_state_get_const(track);
    for (uint8_t osc = 0U; osc < 2U; ++osc)
    {
        cache->prism_model[osc] = (tone != NULL)
            ? (uint8_t)(tone->prism.model[osc] + 0.5f) : 0xFFU;
    }
    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        cache->stack_model[slot] = (tone != NULL)
            ? (uint8_t)(tone->stack.model[slot] + 0.5f) : 0xFFU;
    }
    cache->drum_md_slot_count = (tone != NULL)
        ? track_tone_sound_state_md_slot_count(track) : 0U;
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
        case PARAM_WAVE_OSC2_POS: return "OSC2 POS";
        case PARAM_WAVE_OSC1_START: return "OSC1 START";
        case PARAM_WAVE_OSC2_START: return "OSC2 START";
        case PARAM_WAVE_OSC1_LEN: return "OSC1 LEN";
        case PARAM_WAVE_OSC2_LEN: return "OSC2 LEN";
        case PARAM_WAVE_VOLUME: return "WAVE VOL";
        case PARAM_WAVE_BALANCE: return "WAVE BAL";
        case PARAM_WAVE_TUNE: return "WAVE TUNE";
        case PARAM_WAVE_DETUNE: return "WAVE DETUNE";
        default: return NULL;
    }
}

static const char *mod_destination_prism_label_for_param(param_id_t dest)
{
    switch (dest)
    {
        case PARAM_PRISM_OSC1_AMOD: return "OSC1 AMOD";
        case PARAM_PRISM_TUNE: return "PRISM TUNE";
        case PARAM_PRISM_PITCH_MOD1: return "OSC1 P.MOD";
        case PARAM_PRISM_BALANCE: return "PRISM BAL";
        case PARAM_PRISM_DETUNE: return "PRISM DETUNE";
        case PARAM_PRISM_DRIFT: return "PRISM DRIFT";
        case PARAM_PRISM_OSC2_AMOD: return "OSC2 AMOD";
        case PARAM_PRISM_PITCH_MOD2: return "OSC2 P.MOD";
        default: return NULL;
    }
}

static uint8_t mod_destination_is_dynamic_prism_param(param_id_t dest)
{
    return (uint8_t)((dest == PARAM_PRISM_OSC1_PARAM1)
        || (dest == PARAM_PRISM_OSC1_PARAM2)
        || (dest == PARAM_PRISM_OSC2_PARAM1)
        || (dest == PARAM_PRISM_OSC2_PARAM2));
}

static uint8_t mod_destination_md_label_for_track_param(
    uint8_t track, param_id_t dest, const char **out_label)
{
    if ((out_label == NULL)
            || (dest < PARAM_DRUM_MD_P1)
            || (dest > PARAM_DRUM_MD_P8))
    {
        return 0U;
    }
    const track_tone_sound_state_t *const tone =
        track_tone_sound_state_get_const(track);
    if (tone == NULL)
    {
        return 0U;
    }
    const md_model_profile_t *const profile =
        md_model_profile_get(md_model_validate(tone->md.model));
    const uint8_t slot = (uint8_t)(dest - PARAM_DRUM_MD_P1);
    if ((slot >= profile->slot_count)
            || (profile->slot_labels[slot] == NULL))
    {
        return 0U;
    }
    *out_label = profile->slot_labels[slot];
    return 1U;
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
    char audio_fx_name[24];
    char prism_name[24];
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
    else
    {
        uint8_t fx_slot = 0U;
        uint8_t fx_param = 0U;
        const track_sound_state_t *const state = track_sound_state_get_const(target);
        const uint8_t fx_model = (state == NULL) ? AUDIO_FX_MODEL_OFF
            : (audio_fx_param_catalog_param_info(dest, &fx_slot, &fx_param) != 0U)
                ? ((fx_slot != 0U) ? state->audio_fx_b_model : state->audio_fx_model)
                : AUDIO_FX_MODEL_OFF;
        const char *fx_param_name = NULL;
        if ((audio_fx_param_catalog_param_info(dest, &fx_slot, &fx_param) != 0U)
                && (audio_fx_param_catalog_resolve(
                    fx_model, fx_param, &fx_param_name) != 0U))
        {
            (void)snprintf(audio_fx_name, sizeof(audio_fx_name),
                           "FX %c %s", (fx_slot != 0U) ? 'B' : 'A',
                           fx_param_name);
            name = audio_fx_name;
        }
        else if ((mod_destination_is_dynamic_prism_param(dest) != 0U)
                && (param_prism_label_for_track_param(
                    target, dest, &name) != 0U))
        {
            const uint8_t osc2 = (uint8_t)((dest == PARAM_PRISM_OSC2_PARAM1)
                || (dest == PARAM_PRISM_OSC2_PARAM2));
            (void)snprintf(prism_name, sizeof(prism_name), "OSC%u %s",
                           (unsigned int)osc2 + 1U, name);
            name = prism_name;
        }
        else if (mod_destination_md_label_for_track_param(
                    target, dest, &name) != 0U)
        {
        }
        else if ((name = mod_destination_prism_label_for_param(dest)) == NULL)
        {
            if (param_stack_label_for_track_param(target, dest, &name) == 0U)
            {
                name = mod_destination_wave_label_for_param(dest);
                if (name == NULL)
                {
                    name = param_registry[dest].name;
                }
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
        entity_topology_descriptor_t target_topology;
        if ((entity_topology_get(target, &target_topology) == 0U)
                || (target_topology.active == 0U))
        {
            return 0U;
        }
        const int written = (target_topology.role == ENTITY_ROLE_GROUP_MASTER)
            ? snprintf(out, out_len, "MASTER ")
            : snprintf(out, out_len, "SUB%u ",
                (unsigned int)target_topology.member_index + 1U);
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
        case PARAM_MIX_SEND3: return "Snd3";
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
        case PARAM_PRISM_OSC1_PARAM1: return "O1P1";
        case PARAM_PRISM_OSC1_PARAM2: return "O1P2";
        case PARAM_PRISM_OSC1_AMOD: return "O1AM";
        case PARAM_PRISM_TUNE: return "Tune";
        case PARAM_PRISM_PITCH_MOD1: return "O1PM";
        case PARAM_PRISM_BALANCE: return "Bal";
        case PARAM_PRISM_DETUNE: return "Detn";
        case PARAM_PRISM_DRIFT: return "Drft";
        case PARAM_PRISM_OSC2_PARAM1: return "O2P1";
        case PARAM_PRISM_OSC2_PARAM2: return "O2P2";
        case PARAM_PRISM_OSC2_AMOD: return "O2AM";
        case PARAM_PRISM_PITCH_MOD2: return "O2PM";
        case PARAM_WAVE_OSC1_POS: return "O1Ps";
        case PARAM_WAVE_OSC2_POS: return "O2Ps";
        case PARAM_WAVE_OSC1_START: return "O1St";
        case PARAM_WAVE_OSC2_START: return "O2St";
        case PARAM_WAVE_OSC1_LEN: return "O1Ln";
        case PARAM_WAVE_OSC2_LEN: return "O2Ln";
        case PARAM_WAVE_VOLUME: return "Vol";
        case PARAM_WAVE_BALANCE: return "Bal";
        case PARAM_WAVE_TUNE: return "Tune";
        case PARAM_WAVE_DETUNE: return "Detn";
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
    char audio_fx_label[8];
    uint8_t fx_slot = 0U;
    uint8_t fx_param = 0U;
    const char *fx_param_name = NULL;
    const track_sound_state_t *const fx_state = track_sound_state_get_const(target);
    if ((fx_state != NULL)
            && (audio_fx_param_catalog_param_info(dest, &fx_slot, &fx_param) != 0U)
            && (audio_fx_param_catalog_resolve(
                (fx_slot != 0U) ? fx_state->audio_fx_b_model
                                : fx_state->audio_fx_model,
                fx_param, &fx_param_name) != 0U))
    {
        (void)snprintf(audio_fx_label, sizeof(audio_fx_label), "%c %.2s",
                       (fx_slot != 0U) ? 'B' : 'A', fx_param_name);
        label = audio_fx_label;
    }
    else if (param_stack_label_for_track_param(target, dest, &label) != 0U)
    {
    }
    else if ((mod_destination_is_dynamic_prism_param(dest) != 0U)
            && (param_prism_label_for_track_param(
                target, dest, &label) != 0U))
    {
    }
    else if (mod_destination_md_label_for_track_param(
                target, dest, &label) != 0U)
    {
    }
    else
    {
        label = mod_destination_short_label_for_param(dest);
    }
    if (label == NULL)
    {
        (void)param_prism_label_for_track_param(target, dest, &label);
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
        entity_topology_descriptor_t target_topology;
        if ((entity_topology_get(target, &target_topology) == 0U)
                || (target_topology.active == 0U))
        {
            return 0U;
        }
        out[0] = (target_topology.role == ENTITY_ROLE_GROUP_MASTER)
            ? 'M' : (char)('1' + target_topology.member_index);
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
    mod_destination_catalog_invalidate_all();
}

void audio_mod_destination_catalog_reset_runtime(void)
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

void audio_mod_destination_catalog_invalidate_runtime_value(uint8_t track, param_id_t id)
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
