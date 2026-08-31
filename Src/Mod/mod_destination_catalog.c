#include "Mod/mod_destination_audio.h"

#include <stdio.h>
#include "Platform/memory_layout.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_fx_runtime.h"
#include "Audio/fx_audio_drift.h"

#include "Audio/drum_synth.h"
#include "Param/md_model_catalog.h"
#include "Audio/Engines/prism_engine.h"
#include "Audio/Engines/fm_engine.h"
#include "Audio/Engines/Sampler/brick6_sampler_runtime.h"
#include "Audio/brick6_looper_runtime.h"
#include "Track/synth_polyphony.h"
#include "Audio/Engines/stack_engine.h"
#include "Audio/Engines/wavetable_engine.h"
#include "IPC/control_audio_command.h"
#include "Param/param_filter_audio.h"
#include "Param/param_audio.h"
#include "Param/audio_fx_param_catalog.h"
#include "Param/param_registry_backends.h"
#include "Param/param_prism_labels.h"
#include "Param/param_stack_labels.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_env3.h"
#include "Seq/seq_types.h"
#include "mixer.h"

/* AUDIO destination execution is lane-scoped. */
#undef SEQ_TRACK_COUNT
#define SEQ_TRACK_COUNT SEQ_LANE_CAPACITY

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

static uint8_t mod_destination_is_lfo_rate(param_id_t dest)
{
    return ((dest == PARAM_LFO1_RATE) || (dest == PARAM_LFO2_RATE) || (dest == PARAM_LFO3_RATE)) ? 1U : 0U;
}

static uint8_t mod_destination_apply_simple_mix_rt(uint8_t track,
                                                   param_id_t dest,
                                                   const track_audio_runtime_ctx_t *ctx,
                                                   float value)
{
    if ((track >= SEQ_TRACK_COUNT)
            || (ctx == NULL)
            || (ctx->program_route.active == 0U)
            || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
            || (ctx->program_route.mix_track_id >= MIXER_MAX_TRACKS))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_MIX_LEVEL:
            mixer_set_track_gain(ctx->program_route.mix_track_id, mod_destination_clampf(value, 0.0f, 2.0f));
            return 1U;
        case PARAM_MIX_PAN:
            mixer_set_track_pan(ctx->program_route.mix_track_id, mod_destination_clampf(value, -1.0f, 1.0f));
            return 1U;
        case PARAM_MIX_SEND1:
            mixer_set_track_send_level(ctx->program_route.mix_track_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_MIX_SEND2:
            mixer_set_track_send_level(ctx->program_route.mix_track_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_MIX_SEND3:
            mixer_set_track_send_level(ctx->program_route.mix_track_id, 2U, mod_destination_clampf(value, 0.0f, 1.0f));
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
            || (ctx->program_route.active == 0U)
            || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
            || (ctx->program_route.mix_track_id >= MIXER_MAX_TRACKS)
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
            mixer_set_track_filter_cutoff_modulated(ctx->program_route.mix_track_id,
                                                    param_filter_audio_cutoff_hz(value));
            return 1U;
        case PARAM_FILTER_RESONANCE:
            mixer_set_track_filter_resonance(ctx->program_route.mix_track_id, param_filter_audio_resonance(value));
            return 1U;
        case PARAM_FILTER_EG_AMT:
            mixer_set_track_filter_eg_amount(ctx->program_route.mix_track_id, param_filter_audio_eg_amount(value));
            return 1U;
        case PARAM_FILTER_ATTACK:
            mixer_set_track_filter_attack(ctx->program_route.mix_track_id, param_filter_audio_attack_s(value));
            return 1U;
        case PARAM_FILTER_DECAY:
            mixer_set_track_filter_decay(ctx->program_route.mix_track_id, param_filter_audio_decay_s(value));
            return 1U;
        case PARAM_FILTER_SUSTAIN:
            mixer_set_track_filter_sustain(ctx->program_route.mix_track_id, param_filter_audio_sustain(value));
            return 1U;
        case PARAM_FILTER_RELEASE:
            mixer_set_track_filter_release(ctx->program_route.mix_track_id, param_filter_audio_release_s(value));
            return 1U;
        case PARAM_FILTER_KEYTRK:
            mixer_set_track_filter_keytrack(ctx->program_route.mix_track_id, param_filter_audio_keytrack(value));
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
            || (ctx->program_route.active == 0U)
            || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
            || (ctx->program_route.mix_track_id >= MIXER_MAX_TRACKS)
            || (audio_note_engine_adapter_ctx_supports_vca_gate(ctx) == 0U))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_VCA_ATTACK:
            mixer_set_track_vca_attack(ctx->program_route.mix_track_id, param_filter_audio_attack_s(value));
            return 1U;
        case PARAM_VCA_DECAY:
            mixer_set_track_vca_decay(ctx->program_route.mix_track_id, param_filter_audio_decay_s(value));
            return 1U;
        case PARAM_VCA_SUSTAIN:
            mixer_set_track_vca_sustain(ctx->program_route.mix_track_id, param_filter_audio_sustain(value));
            return 1U;
        case PARAM_VCA_RELEASE:
        {
            const float release_s = param_filter_audio_release_s(value);
            mixer_set_track_vca_release(ctx->program_route.mix_track_id, release_s);
            if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            {
                brick6_braids_runtime_set_vca_release_seconds(ctx->program_route.instance_id, release_s);
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
            || (ctx->program_route.active == 0U)
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
            brick6_looper_runtime_set_main_xfade(track, mod_destination_clampf(value, 0.0f, 1.0f));
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
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_PRISM))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_PRISM_TUNE:
            brick6_braids_runtime_set_tune(ctx->program_route.instance_id, mod_destination_clampf(value, -60.0f, 60.0f));
            return 1U;
        case PARAM_PRISM_DETUNE:
            brick6_braids_runtime_set_detune(ctx->program_route.instance_id, mod_destination_clampf(value, -24.0f, 24.0f));
            return 1U;
        case PARAM_PRISM_DRIFT:
            brick6_braids_runtime_set_drift(ctx->program_route.instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_PITCH_MOD1:
            brick6_braids_runtime_set_osc_pitch_mod(ctx->program_route.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC1_PARAM1:
            brick6_braids_runtime_set_osc_timbre(ctx->program_route.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC1_AMOD:
            brick6_braids_runtime_set_osc_modulation(ctx->program_route.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC1_PARAM2:
            brick6_braids_runtime_set_osc_color(ctx->program_route.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_BALANCE:
            brick6_braids_runtime_set_balance(ctx->program_route.instance_id, mod_destination_clampf(value, -1.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_PITCH_MOD2:
            brick6_braids_runtime_set_osc_pitch_mod(ctx->program_route.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_PARAM1:
            brick6_braids_runtime_set_osc_timbre(ctx->program_route.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_AMOD:
            brick6_braids_runtime_set_osc_modulation(ctx->program_route.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_PRISM_OSC2_PARAM2:
            brick6_braids_runtime_set_osc_color(ctx->program_route.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
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
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_FM)
            || (mod_destination_is_direct_fm(dest) == 0U))
    {
        return 0U;
    }
    return param_audio_apply_track_rt(dest, track, value);
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
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            || (ctx->program_route.instance_id >= BRICK6_STACK_MAX_INSTANCES))
    {
        return 0U;
    }

    if (dest == PARAM_STACK_NOISE_LEVEL)
    {
        brick6_stack_runtime_set_noise_level(ctx->program_route.instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
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
            brick6_stack_runtime_set_slot_level(ctx->program_route.instance_id, slot, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case 2U:
        {
            const float clamped = mod_destination_clampf(value, -24.0f, 24.0f);
            brick6_stack_runtime_set_slot_tune(ctx->program_route.instance_id,
                                               slot,
                                               clamped);
            return 1U;
        }
        case 3U:
            brick6_stack_runtime_set_slot_timbre(ctx->program_route.instance_id, slot, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case 4U:
            brick6_stack_runtime_set_slot_color(ctx->program_route.instance_id, slot, mod_destination_clampf(value, 0.0f, 1.0f));
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
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            || (ctx->program_route.instance_id >= BRICK6_WAVE_MAX_INSTANCES))
    {
        return 0U;
    }

    switch (dest)
    {
        case PARAM_WAVE_OSC1_POS:
            brick6_wave_runtime_set_osc_pos(ctx->program_route.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC2_POS:
            brick6_wave_runtime_set_osc_pos(ctx->program_route.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC1_START:
            brick6_wave_runtime_set_osc_start(ctx->program_route.instance_id, 0U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC2_START:
            brick6_wave_runtime_set_osc_start(ctx->program_route.instance_id, 1U, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC1_LEN:
            brick6_wave_runtime_set_osc_len(ctx->program_route.instance_id, 0U, mod_destination_clampf(value, 0.01f, 1.0f));
            return 1U;
        case PARAM_WAVE_OSC2_LEN:
            brick6_wave_runtime_set_osc_len(ctx->program_route.instance_id, 1U, mod_destination_clampf(value, 0.01f, 1.0f));
            return 1U;
        case PARAM_WAVE_VOLUME:
            brick6_wave_runtime_set_volume(ctx->program_route.instance_id, mod_destination_clampf(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_BALANCE:
            brick6_wave_runtime_set_balance(ctx->program_route.instance_id, mod_destination_clampf(value, -1.0f, 1.0f));
            return 1U;
        case PARAM_WAVE_TUNE:
            brick6_wave_runtime_set_tune(ctx->program_route.instance_id, mod_destination_clampf(value, -60.0f, 60.0f));
            return 1U;
        case PARAM_WAVE_DETUNE:
            brick6_wave_runtime_set_detune(ctx->program_route.instance_id, mod_destination_clampf(value, -24.0f, 24.0f));
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
            || (ctx->program_route.active == 0U)
            || (ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
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
            return drum_synth_set_param_for_instance(ctx->program_route.instance_id,
                                                     dest,
                                                     mod_destination_clampf(value, 0.0f, 127.0f));
        case PARAM_DRUM_TRX_BD_PITCH:
            return drum_synth_set_param_for_instance(ctx->program_route.instance_id,
                                                     dest,
                                                     mod_destination_clampf(value, -48.0f, 24.0f));
        case PARAM_DRUM_TRX_BD_DECAY:
            return drum_synth_set_param_for_instance(ctx->program_route.instance_id,
                                                     dest,
                                                     mod_destination_clampf(value, 0.01f, 2.0f));
        case PARAM_DRUM_TRX_BD_HARMONICS:
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
            return drum_synth_set_param_for_instance(ctx->program_route.instance_id,
                                                     dest,
                                                     mod_destination_clampf(value, 0.0f, 1.0f));
        default:
            return 0U;
    }
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
            if (mod_destination_is_direct_drum(dest) != 0U)
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
                                        const mod_destination_audio_models_t *models,
                                        mod_destination_prepared_t *out)
{
    if ((target >= SEQ_TRACK_COUNT) || (dest >= PARAM_COUNT)
            || (ctx == NULL) || (out == NULL)
            || (ctx->program_route.active == 0U))
        return 0U;

    mod_destination_prepared_t prepared = {
        .param = (uint16_t)dest,
        .target = target,
        .endpoint = ctx->program_route.instance_id,
        .aux = ctx->program_route.engine
    };
    if (mod_destination_prepared_opcode(dest, &prepared.opcode,
                                        &prepared.subindex) == 0U)
        return 0U;
    if (prepared.opcode == MOD_DEST_APPLY_AUDIO_FX_DELAY)
    {
        const uint8_t model = (models != NULL)
            ? models->audio_fx_model[(prepared.subindex != 0U) ? 1U : 0U]
            : AUDIO_FX_MODEL_OFF;
        if (model != AUDIO_FX_MODEL_DRIFT)
            prepared.opcode = MOD_DEST_APPLY_GENERIC;
    }
    if ((prepared.opcode >= MOD_DEST_APPLY_MIX_LEVEL)
            && (prepared.opcode <= MOD_DEST_APPLY_VCA_RELEASE))
        prepared.endpoint = ctx->program_route.mix_track_id;
    if (prepared.opcode == MOD_DEST_APPLY_VCA_RELEASE)
        prepared.subindex = ctx->program_route.instance_id;
    if (prepared.opcode == MOD_DEST_APPLY_SAMPLER_GAIN)
        prepared.aux = ctx->type;
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
        case MOD_DEST_APPLY_FILTER_CUTOFF: mixer_set_track_filter_cutoff_modulated(p->endpoint, param_filter_audio_cutoff_hz(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_RESONANCE: mixer_set_track_filter_resonance(p->endpoint, param_filter_audio_resonance(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_EG_AMOUNT: mixer_set_track_filter_eg_amount(p->endpoint, param_filter_audio_eg_amount(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_ATTACK: mixer_set_track_filter_attack(p->endpoint, param_filter_audio_attack_s(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_DECAY: mixer_set_track_filter_decay(p->endpoint, param_filter_audio_decay_s(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_SUSTAIN: mixer_set_track_filter_sustain(p->endpoint, param_filter_audio_sustain(value)); return 1U;
        case MOD_DEST_APPLY_FILTER_RELEASE: mixer_set_track_filter_release(p->endpoint, param_filter_audio_release_s(value)); return 1U;
        case MOD_DEST_APPLY_VCA_ATTACK: mixer_set_track_vca_attack(p->endpoint, param_filter_audio_attack_s(value)); return 1U;
        case MOD_DEST_APPLY_VCA_DECAY: mixer_set_track_vca_decay(p->endpoint, param_filter_audio_decay_s(value)); return 1U;
        case MOD_DEST_APPLY_VCA_SUSTAIN: mixer_set_track_vca_sustain(p->endpoint, param_filter_audio_sustain(value)); return 1U;
        case MOD_DEST_APPLY_VCA_RELEASE:
        {
            const float seconds = param_filter_audio_release_s(value);
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
        case MOD_DEST_APPLY_LOOPER_XFADE: brick6_looper_runtime_set_main_xfade(p->target, mod_destination_clampf(value, 0.0f, 1.0f)); return 1U;
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
        case MOD_DEST_APPLY_AUDIO_FX_DELAY:
            return audio_fx_runtime_apply_drift_delay_modulated(
                (brick_entity_id_t)p->target,
                (p->subindex != 0U) ? PARAM_AUDIO_FX_B_P1 : PARAM_AUDIO_FX_P1,
                mod_destination_clampf(value,0.0f,FX_AUDIO_DRIFT_DELAY_MOD_MAX_CONTROL));
        case MOD_DEST_APPLY_GENERIC: return param_audio_apply_track_rt((param_id_t)p->param, p->target, value);
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
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_cutoff(multi_slot, param_filter_audio_cutoff_hz(value));
            else mixer_poly_voice_set_cutoff(voice_slot, param_filter_audio_cutoff_hz(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_RESONANCE:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_resonance(multi_slot, param_filter_audio_resonance(value));
            else mixer_poly_voice_set_resonance(voice_slot, param_filter_audio_resonance(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_EG_AMOUNT:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_eg_amount(multi_slot, param_filter_audio_eg_amount(value));
            else mixer_poly_voice_set_eg_amount(voice_slot, param_filter_audio_eg_amount(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_ATTACK:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_env_attack(multi_slot, param_filter_audio_attack_s(value));
            else mixer_poly_voice_set_filter_attack(voice_slot, param_filter_audio_attack_s(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_DECAY:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_env_decay(multi_slot, param_filter_audio_decay_s(value));
            else mixer_poly_voice_set_filter_decay(voice_slot, param_filter_audio_decay_s(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_SUSTAIN:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_env_sustain(multi_slot, param_filter_audio_sustain(value));
            else mixer_poly_voice_set_filter_sustain(voice_slot, param_filter_audio_sustain(value));
            return 1U;
        case MOD_DEST_APPLY_FILTER_RELEASE:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_env_release(multi_slot, param_filter_audio_release_s(value));
            else mixer_poly_voice_set_filter_release(voice_slot, param_filter_audio_release_s(value));
            return 1U;
        case MOD_DEST_APPLY_VCA_ATTACK:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_vca_attack(multi_slot, param_filter_audio_attack_s(value));
            else mixer_poly_voice_set_vca_attack(voice_slot, param_filter_audio_attack_s(value));
            return 1U;
        case MOD_DEST_APPLY_VCA_DECAY:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_vca_decay(multi_slot, param_filter_audio_decay_s(value));
            else mixer_poly_voice_set_vca_decay(voice_slot, param_filter_audio_decay_s(value));
            return 1U;
        case MOD_DEST_APPLY_VCA_SUSTAIN:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_vca_sustain(multi_slot, param_filter_audio_sustain(value));
            else mixer_poly_voice_set_vca_sustain(voice_slot, param_filter_audio_sustain(value));
            return 1U;
        case MOD_DEST_APPLY_VCA_RELEASE:
            if (sampler_multi != 0U) mixer_multi_filter_set_voice_vca_release(multi_slot, param_filter_audio_release_s(value));
            else mixer_poly_voice_set_vca_release(voice_slot, param_filter_audio_release_s(value));
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
    return param_audio_apply_track_rt(dest, track, value);
}

uint8_t mod_destination_catalog_apply_poly_voice_rt(uint8_t track,
                                                    uint8_t voice_slot,
                                                    param_id_t dest,
                                                    const track_audio_runtime_ctx_t *ctx,
                                                    float value)
{
    if ((ctx == NULL) || (ctx->program_route.active == 0U))
    {
        return 0U;
    }

    track_audio_runtime_ctx_t voice_ctx = *ctx;
    voice_ctx.program_route.instance_id = voice_slot;
    if ((ctx->program_route.engine != (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            && (mod_destination_is_direct_filter(dest) != 0U
                || mod_destination_is_direct_vca(dest) != 0U))
    {
        switch (dest)
        {
            case PARAM_FILTER_CUTOFF:
            {
                const float cutoff_hz = param_filter_audio_cutoff_hz(value);
                mixer_poly_voice_set_cutoff(voice_slot, cutoff_hz);
                return 1U;
            }
            case PARAM_FILTER_RESONANCE:
                mixer_poly_voice_set_resonance(voice_slot, param_filter_audio_resonance(value)); return 1U;
            case PARAM_FILTER_EG_AMT:
                mixer_poly_voice_set_eg_amount(voice_slot, param_filter_audio_eg_amount(value)); return 1U;
            case PARAM_FILTER_ATTACK:
                mixer_poly_voice_set_filter_attack(voice_slot, param_filter_audio_attack_s(value)); return 1U;
            case PARAM_FILTER_DECAY:
                mixer_poly_voice_set_filter_decay(voice_slot, param_filter_audio_decay_s(value)); return 1U;
            case PARAM_FILTER_SUSTAIN:
                mixer_poly_voice_set_filter_sustain(voice_slot, param_filter_audio_sustain(value)); return 1U;
            case PARAM_FILTER_RELEASE:
                mixer_poly_voice_set_filter_release(voice_slot, param_filter_audio_release_s(value)); return 1U;
            case PARAM_VCA_ATTACK:
                mixer_poly_voice_set_vca_attack(voice_slot, param_filter_audio_attack_s(value)); return 1U;
            case PARAM_VCA_DECAY:
                mixer_poly_voice_set_vca_decay(voice_slot, param_filter_audio_decay_s(value)); return 1U;
            case PARAM_VCA_SUSTAIN:
                mixer_poly_voice_set_vca_sustain(voice_slot, param_filter_audio_sustain(value)); return 1U;
            case PARAM_VCA_RELEASE:
                mixer_poly_voice_set_vca_release(voice_slot, param_filter_audio_release_s(value)); return 1U;
            default: return 0U;
        }
    }
    switch ((track_runtime_engine_t)ctx->program_route.engine)
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
                    mixer_multi_filter_set_voice_cutoff(slot, param_filter_audio_cutoff_hz(value));
                    return 1U;
                case PARAM_FILTER_RESONANCE:
                    mixer_multi_filter_set_voice_resonance(slot, param_filter_audio_resonance(value));
                    return 1U;
                case PARAM_FILTER_EG_AMT:
                    mixer_multi_filter_set_voice_eg_amount(slot, param_filter_audio_eg_amount(value));
                    return 1U;
                case PARAM_FILTER_ATTACK:
                    mixer_multi_filter_set_voice_env_attack(slot, param_filter_audio_attack_s(value));
                    return 1U;
                case PARAM_FILTER_DECAY:
                    mixer_multi_filter_set_voice_env_decay(slot, param_filter_audio_decay_s(value));
                    return 1U;
                case PARAM_FILTER_SUSTAIN:
                    mixer_multi_filter_set_voice_env_sustain(slot, param_filter_audio_sustain(value));
                    return 1U;
                case PARAM_FILTER_RELEASE:
                    mixer_multi_filter_set_voice_env_release(slot, param_filter_audio_release_s(value));
                    return 1U;
                case PARAM_VCA_ATTACK:
                    mixer_multi_filter_set_voice_vca_attack(slot, param_filter_audio_attack_s(value));
                    return 1U;
                case PARAM_VCA_DECAY:
                    mixer_multi_filter_set_voice_vca_decay(slot, param_filter_audio_decay_s(value));
                    return 1U;
                case PARAM_VCA_SUSTAIN:
                    mixer_multi_filter_set_voice_vca_sustain(slot, param_filter_audio_sustain(value));
                    return 1U;
                case PARAM_VCA_RELEASE:
                    mixer_multi_filter_set_voice_vca_release(slot, param_filter_audio_release_s(value));
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
        return ((ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
                || (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
                || (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
                || ((ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
                    && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))) ? 1U : 0U;
    }
    switch ((track_runtime_engine_t)ctx->program_route.engine)
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

uint8_t mod_destination_catalog_supported_audio(uint8_t track,
                                                param_id_t dest,
                                                track_family_t family,
                                                track_type_t type,
                                                const track_audio_runtime_ctx_t *ctx,
                                                const mod_destination_audio_models_t *models)
{
    (void)family;
    (void)type;
    (void)models;
    /* CONTROL owns destination policy.  AUDIO checks only the command ABI and
     * resolves the already-authorized destination to a terminal DSP opcode. */
    return ((track < SEQ_TRACK_COUNT) && (dest < PARAM_COUNT)
            && (ctx != NULL) && (ctx->program_route.active != 0U)) ? 1U : 0U;
}
void audio_mod_destination_catalog_reset_runtime(void) {}

void audio_mod_destination_catalog_invalidate_runtime_value(uint8_t track, param_id_t id)
{
    (void)track;
    (void)id;
}
