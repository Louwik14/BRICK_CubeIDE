#include "Mod/mod_lfo_v1.h"

#include <math.h>
#include <string.h>

#include "Audio/audio_xfade.h"
#include "Audio/drum_synth.h"
#include "Core/brick6_audio_event_grid.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/track_sound_state.h"
#include "Core/track_runtime.h"
#include "Param/param_filter.h"
#include "Param/param_registry.h"
#include "Param/param_registry_backends.h"
#include "Param/param_wave_labels.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "mixer.h"
#include "ui_core.h"

#define MOD_LFO_COUNT_PER_TRACK 2U
#define MOD_LFO_SYNC_RATE_COUNT 15U
#define MOD_LFO_AUDIO_SAMPLE_RATE 48000.0f
#define MOD_LFO_CONTROL_RATE_HZ 3000.0f
#define MOD_LFO_LEGACY_CONTROL_STRIDE ((uint32_t)(MOD_LFO_AUDIO_SAMPLE_RATE / MOD_LFO_CONTROL_RATE_HZ))
#define MOD_LFO_WINDOW_RATE_EXPERIMENT 1U
#ifndef MOD_LFO_WINDOW_RATE_FRAMES
#define MOD_LFO_WINDOW_RATE_FRAMES BRICK6_AUDIO_EVENT_GRID_FRAMES
#endif
#if MOD_LFO_WINDOW_RATE_EXPERIMENT
#define MOD_LFO_CONTROL_STRIDE ((uint32_t)MOD_LFO_WINDOW_RATE_FRAMES)
#define MOD_LFO_PHASE_DT (1.0f / MOD_LFO_AUDIO_SAMPLE_RATE)
#else
#define MOD_LFO_CONTROL_STRIDE MOD_LFO_LEGACY_CONTROL_STRIDE
#define MOD_LFO_PHASE_DT (1.0f / MOD_LFO_CONTROL_RATE_HZ)
#endif
#define MOD_LFO_DEST_NONE ((param_id_t)PARAM_COUNT)
#define MOD_LFO_SINE_LUT_SIZE 256U
#define MOD_LFO_RATE_OFF_EPS 0.0001f

/* Positive RATE values index this tempo-sync table: 1=8BAR ... 15=1/64. */
static const float g_mod_lfo_sync_bars_per_cycle[MOD_LFO_SYNC_RATE_COUNT] = {
    8.0f, 4.0f, 2.0f, 1.0f,
    0.5f, 0.33333334f,
    0.25f, 0.16666667f,
    0.125f, 0.08333334f,
    0.0625f, 0.04166667f,
    0.03125f, 0.020833334f,
    0.015625f
};

static const float g_mod_lfo_sine_lut[MOD_LFO_SINE_LUT_SIZE + 1U] = {
#include "mod_lfo_sine_lut_257.inc"
};

typedef struct
{
    uint32_t phase;
    uint32_t phase_inc;
    uint32_t delay_remaining_frames;
    uint32_t fade_elapsed_frames;
    float current;
    float hold_value;
    float slew_value;
    uint32_t rng_state;
    float sh_value;
    uint8_t sh_valid;
    uint8_t triggered;
    uint8_t one_running;
    uint8_t one_done;
    uint8_t hold_valid;
    uint8_t hold_capture_pending;
    uint8_t slew_valid;
    uint8_t active;
    uint16_t last_dest;
    float base_value;
    float dest_min;
    float dest_max;
    float depth_scale;
    uint8_t base_valid;
    uint8_t calib_valid;
    uint8_t temp_valid_mask;
    track_mod_lfo_state_t temp;
} mod_lfo_runtime_state_t;

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
} mod_lfo_dest_cache_t;

typedef struct
{
    uint8_t valid;
    uint8_t value;
} mod_lfo_midi_cc_cache_t;

static mod_lfo_runtime_state_t g_mod_lfo_runtime[SEQ_TRACK_COUNT][MOD_LFO_COUNT_PER_TRACK];
static mod_lfo_dest_cache_t g_mod_lfo_dest_cache[SEQ_TRACK_COUNT];
static mod_lfo_midi_cc_cache_t g_mod_lfo_midi_cc_cache[SEQ_TRACK_COUNT][12U];
volatile uint32_t g_mod_lfo_dest_cache_invalidate_all_count;
volatile uint32_t g_mod_lfo_dest_cache_invalidate_track_count[SEQ_TRACK_COUNT];
static uint32_t g_mod_lfo_control_counter = 0U;

static float mod_lfo_clampf(float v, float lo, float hi)
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

static uint8_t mod_lfo_is_simple_mix_dest(param_id_t dest)
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

static uint8_t mod_lfo_is_direct_filter_dest(param_id_t dest)
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

static uint8_t mod_lfo_is_direct_vca_dest(param_id_t dest)
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

static uint8_t mod_lfo_is_direct_sampler_dest(param_id_t dest)
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

static uint8_t mod_lfo_is_direct_wave_dest(param_id_t dest)
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

static uint8_t mod_lfo_is_direct_drum_dest(param_id_t dest)
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

static uint8_t mod_lfo_is_direct_midi_cc_dest(param_id_t dest)
{
    return param_backend_is_midi_cc_id(dest);
}

static uint8_t mod_lfo_midi_cc_cache_index(param_id_t dest, uint8_t *out_index)
{
    if ((out_index == NULL) || (dest < PARAM_MIDI_CC1_1) || (dest > PARAM_MIDI_CC3_4))
    {
        return 0U;
    }

    *out_index = (uint8_t)(dest - PARAM_MIDI_CC1_1);
    return (*out_index < 12U) ? 1U : 0U;
}

static uint8_t mod_lfo_apply_simple_mix_rt(uint8_t track,
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
            mixer_set_track_gain(ctx->mix_track_id, mod_lfo_clampf(value, 0.0f, 2.0f));
            return 1U;

        case PARAM_MIX_PAN:
            mixer_set_track_pan(ctx->mix_track_id, mod_lfo_clampf(value, -1.0f, 1.0f));
            return 1U;

        case PARAM_MIX_SEND1:
            mixer_set_track_send_level(ctx->mix_track_id, 0U, mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        case PARAM_MIX_SEND2:
            mixer_set_track_send_level(ctx->mix_track_id, 1U, mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t mod_lfo_apply_filter_rt(uint8_t track,
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

static uint8_t mod_lfo_apply_vca_rt(uint8_t track,
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

static uint8_t mod_lfo_apply_sampler_rt(uint8_t track,
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
                brick6_sampler_runtime_set_multi_gain(track, mod_lfo_clampf(value, 0.0f, 2.0f));
            }
            else
            {
                brick6_sampler_runtime_set_gain(track, mod_lfo_clampf(value, 0.0f, 2.0f));
            }
            return 1U;

        case PARAM_SAMPLER_START:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER)
            {
                return 0U;
            }
            brick6_sampler_runtime_set_start(track, mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        case PARAM_SAMPLER_END:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER)
            {
                return 0U;
            }
            brick6_sampler_runtime_set_end(track, mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        case PARAM_SAMPLER_LOOP_START:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER)
            {
                return 0U;
            }
            brick6_sampler_runtime_set_loop_start(track, mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        case PARAM_SAMPLER_MODE:
        {
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER)
            {
                return 0U;
            }
            const uint8_t mode = (uint8_t)(mod_lfo_clampf(value, 0.0f, 3.0f) + 0.5f);
            brick6_sampler_runtime_set_mode(track, mode);
            return 1U;
        }

        case PARAM_SAMPLER_TUNE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER)
            {
                return 0U;
            }
            brick6_sampler_runtime_set_tune(track, mod_lfo_clampf(value, -24.0f, 24.0f));
            return 1U;

        case PARAM_SAMPLER_SLICE_COUNT:
        {
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SAMPLER)
            {
                return 0U;
            }
            static const uint8_t counts[] = {0U, 2U, 4U, 8U, 16U, 32U, 64U};
            const uint8_t idx = (uint8_t)(mod_lfo_clampf(value, 0.0f, 6.0f) + 0.5f);
            brick6_sampler_runtime_set_slice_count(track, counts[idx]);
            return 1U;
        }

        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP)
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_source_bpm(track, mod_lfo_clampf(value, 40.0f, 300.0f));
            return 1U;

        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP)
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_sync_length(track, (uint8_t)(mod_lfo_clampf(value, 0.0f, 4.0f) + 0.5f));
            return 1U;

        case PARAM_SAMPLER_CLIP_PITCH:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP)
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_pitch(track, mod_lfo_clampf(value, -12.0f, 12.0f));
            return 1U;

        case PARAM_SAMPLER_CLIP_PLAY_MODE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP)
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_play_mode(track, (uint8_t)(mod_lfo_clampf(value, 0.0f, 1.0f) + 0.5f));
            return 1U;

        case PARAM_SAMPLER_CLIP_LOOP:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP)
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_loop(track, (uint8_t)(mod_lfo_clampf(value, 0.0f, 1.0f) + 0.5f));
            return 1U;

        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP)
            {
                return 0U;
            }
            brick6_sampler_runtime_set_clip_stretch_mode(track, (uint8_t)(mod_lfo_clampf(value, 0.0f, 2.0f) + 0.5f));
            return 1U;

        case PARAM_SAMPLER_CLIP_GRAIN:
        {
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP)
            {
                return 0U;
            }
            static const uint16_t grain_frames[] = {384U, 512U, 768U, 1024U, 1536U, 2048U};
            const uint8_t idx = (uint8_t)(mod_lfo_clampf(value, 0.0f, 5.0f) + 0.5f);
            brick6_sampler_runtime_set_clip_grain_size(track, grain_frames[idx]);
            return 1U;
        }

        case PARAM_SAMPLER_CLIP_HOP:
        case PARAM_SAMPLER_CLIP_SEARCH:
            return (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_CLIP) ? 1U : 0U;

        case PARAM_SAMPLER_MULTI_LOOP:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_MULTI)
            {
                return 0U;
            }
            brick6_sampler_runtime_set_multi_loop(track, (mod_lfo_clampf(value, 0.0f, 1.0f) >= 0.5f) ? 1U : 0U);
            return 1U;

        case PARAM_LOOPER_XFADE:
            if (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_LOOPER)
            {
                return 0U;
            }
            audio_xfade_set(mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t mod_lfo_apply_wave_rt(uint8_t track,
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
                                           (float)(uint8_t)(mod_lfo_clampf(value, 0.0f, 38.0f) + 0.5f));
            return 1U;

        case PARAM_WAVE_FINE:
            brick6_braids_runtime_set_fine(ctx->instance_id, mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        case PARAM_WAVE_COARSE:
            brick6_braids_runtime_set_coarse(ctx->instance_id, mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        case PARAM_WAVE_FM:
            brick6_braids_runtime_set_fm(ctx->instance_id, mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        case PARAM_WAVE_TIMBRE:
            brick6_braids_runtime_set_timbre(ctx->instance_id, mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        case PARAM_WAVE_MODULATION:
            brick6_braids_runtime_set_modulation(ctx->instance_id, mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        case PARAM_WAVE_COLOR:
            brick6_braids_runtime_set_color(ctx->instance_id, mod_lfo_clampf(value, 0.0f, 1.0f));
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t mod_lfo_apply_drum_rt(uint8_t track,
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
                                                     mod_lfo_clampf(value, -48.0f, 24.0f));

        case PARAM_DRUM_TRX_BD_DECAY:
            return drum_synth_set_param_for_instance(ctx->instance_id,
                                                     dest,
                                                     mod_lfo_clampf(value, 0.01f, 2.0f));

        case PARAM_DRUM_TRX_BD_HARMONICS:
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
            return drum_synth_set_param_for_instance(ctx->instance_id,
                                                     dest,
                                                     mod_lfo_clampf(value, 0.0f, 1.0f));

        default:
            return 0U;
    }
}

static uint8_t mod_lfo_apply_midi_cc_rt(uint8_t track,
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
            || (mod_lfo_is_direct_midi_cc_dest(dest) == 0U))
    {
        return 0U;
    }

    uint8_t cache_index = 0U;
    if (mod_lfo_midi_cc_cache_index(dest, &cache_index) == 0U)
    {
        return 0U;
    }

    const uint8_t cc_value = (uint8_t)(mod_lfo_clampf(value, 0.0f, 127.0f) + 0.5f);
    mod_lfo_midi_cc_cache_t *const cache = &g_mod_lfo_midi_cc_cache[track][cache_index];
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

static uint8_t mod_lfo_apply_destination_rt(uint8_t track,
                                            param_id_t dest,
                                            const track_runtime_ctx_t *ctx,
                                            float value)
{
    if (mod_lfo_is_simple_mix_dest(dest) != 0U)
    {
        return mod_lfo_apply_simple_mix_rt(track, dest, ctx, value);
    }
    if (mod_lfo_is_direct_filter_dest(dest) != 0U)
    {
        return mod_lfo_apply_filter_rt(track, dest, ctx, value);
    }
    if (mod_lfo_is_direct_vca_dest(dest) != 0U)
    {
        return mod_lfo_apply_vca_rt(track, dest, ctx, value);
    }
    if (mod_lfo_is_direct_sampler_dest(dest) != 0U)
    {
        return mod_lfo_apply_sampler_rt(track, dest, ctx, value);
    }
    if (mod_lfo_is_direct_wave_dest(dest) != 0U)
    {
        return mod_lfo_apply_wave_rt(track, dest, ctx, value);
    }
    if (mod_lfo_is_direct_drum_dest(dest) != 0U)
    {
        return mod_lfo_apply_drum_rt(track, dest, ctx, value);
    }
    if (mod_lfo_is_direct_midi_cc_dest(dest) != 0U)
    {
        return mod_lfo_apply_midi_cc_rt(track, dest, ctx, value);
    }

    return param_registry_apply_track_value_rt_fast(dest, track, value);
}

static track_mod_lfo_state_t *mod_lfo_track_settings_mut(uint8_t track, uint8_t lfo_index)
{
    track_sound_state_t *const state = track_sound_state_get(track);

    if ((state == NULL) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK))
    {
        return NULL;
    }

    return &state->mod_lfo[lfo_index];
}

static const track_mod_lfo_state_t *mod_lfo_track_settings_const(uint8_t track, uint8_t lfo_index)
{
    const track_sound_state_t *const state = track_sound_state_get_const(track);

    if ((state == NULL) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK))
    {
        return NULL;
    }

    return &state->mod_lfo[lfo_index];
}

static param_id_t mod_lfo_track_settings_dest(uint8_t track, uint8_t lfo_index)
{
    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    if (s == NULL)
    {
        return MOD_LFO_DEST_NONE;
    }

    return (param_id_t)((uint16_t)(s->dest + 0.5f));
}

static uint8_t mod_lfo_runtime_param_mask(mod_lfo_param_t param)
{
    return (uint8_t)(1U << (uint8_t)param);
}

static float mod_lfo_effective_field(const mod_lfo_runtime_state_t *rt,
                                     const track_mod_lfo_state_t *s,
                                     mod_lfo_param_t param)
{
    const uint8_t mask = mod_lfo_runtime_param_mask(param);
    const track_mod_lfo_state_t *const source =
        ((rt != NULL) && ((rt->temp_valid_mask & mask) != 0U)) ? &rt->temp : s;

    if (source == NULL)
    {
        return 0.0f;
    }

    switch (param)
    {
        case MOD_LFO_PARAM_DEST:
            return source->dest;
        case MOD_LFO_PARAM_RATE:
            return source->rate;
        case MOD_LFO_PARAM_DEPTH:
            return source->depth;
        case MOD_LFO_PARAM_SHAPE:
            return source->shape;
        case MOD_LFO_PARAM_DELAY:
            return source->delay;
        case MOD_LFO_PARAM_TRIG:
            return source->trig;
        case MOD_LFO_PARAM_FADE:
            return source->fade;
        case MOD_LFO_PARAM_PHASE_SLEW:
            return source->phase_slew;
        default:
            return 0.0f;
    }
}

static param_id_t mod_lfo_effective_dest(uint8_t track, uint8_t lfo_index)
{
    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    const mod_lfo_runtime_state_t *const rt =
        ((track < SEQ_TRACK_COUNT) && (lfo_index < MOD_LFO_COUNT_PER_TRACK)) ? &g_mod_lfo_runtime[track][lfo_index] : NULL;

    if (s == NULL)
    {
        return MOD_LFO_DEST_NONE;
    }

    return (param_id_t)((uint16_t)(mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_DEST) + 0.5f));
}

static uint8_t mod_lfo_is_internal_param(param_id_t id)
{
    switch (id)
    {
        case PARAM_LFO1_DEST:
        case PARAM_LFO1_RATE:
        case PARAM_LFO1_DEPTH:
        case PARAM_LFO1_SHAPE:
        case PARAM_LFO1_DELAY:
        case PARAM_LFO1_TRIG:
        case PARAM_LFO1_FADE:
        case PARAM_LFO1_PHASE_SLEW:
        case PARAM_LFO2_DEST:
        case PARAM_LFO2_RATE:
        case PARAM_LFO2_DEPTH:
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

static uint8_t mod_lfo_param_matches_track_context(ui_track_family_t family,
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
        if (dest == PARAM_WAVE_PHASE_RESET)
        {
            return 0U;
        }
        if (dest == PARAM_MIDI_PROGRAM)
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

        if (mod_lfo_is_direct_vca_dest(dest) != 0U)
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

static track_runtime_param_status_t mod_lfo_effective_status_from_ctx(const track_runtime_ctx_t *ctx,
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

static uint8_t mod_lfo_dest_supported_fast(uint8_t track,
                                           param_id_t dest,
                                           ui_track_family_t family,
                                           ui_track_type_t type,
                                           const track_runtime_ctx_t *ctx)
{
    if ((track >= SEQ_TRACK_COUNT) || (dest >= PARAM_COUNT) || (mod_lfo_is_internal_param(dest) != 0U))
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

    if (mod_lfo_param_matches_track_context(family, dest, rule.domain, ctx) == 0U)
    {
        return 0U;
    }

    const track_runtime_param_status_t status = mod_lfo_effective_status_from_ctx(ctx, rule.resource);
    return ((status == TRACK_RUNTIME_PARAM_ALLOWED) || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

static void mod_lfo_dest_cache_invalidate_track_internal(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_mod_lfo_dest_cache[track].valid = 0U;
}

void mod_lfo_v1_invalidate_dest_cache_track(uint8_t track)
{
    mod_lfo_dest_cache_invalidate_track_internal(track);
    if (track < SEQ_TRACK_COUNT)
    {
        g_mod_lfo_dest_cache_invalidate_track_count[track]++;
    }
}

void mod_lfo_v1_invalidate_dest_cache_all(void)
{
    g_mod_lfo_dest_cache_invalidate_all_count++;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        mod_lfo_dest_cache_invalidate_track_internal(track);
    }
}

static uint8_t mod_lfo_dest_cache_matches_context(const mod_lfo_dest_cache_t *cache,
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
            && (cache->rt_mix_track_id == ctx_mix_track_id))
            ? 1U
            : 0U;
}

static mod_lfo_dest_cache_t *mod_lfo_dest_cache_resolve(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }

    track_runtime_refresh_track(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    const ui_track_family_t family = ui_get_track_family(track);
    const ui_track_type_t type = ui_get_track_type(track);
    mod_lfo_dest_cache_t *const cache = &g_mod_lfo_dest_cache[track];

    if (mod_lfo_dest_cache_matches_context(cache, family, type, ctx) != 0U)
    {
        return cache;
    }

    cache->index_to_param[0] = MOD_LFO_DEST_NONE;
    for (uint16_t raw = 0U; raw < (uint16_t)PARAM_COUNT; ++raw)
    {
        cache->param_to_index[raw] = 0U;
    }

    uint16_t count = 1U;
    for (uint16_t raw = 0U; raw < (uint16_t)PARAM_COUNT; ++raw)
    {
        const param_id_t param = (param_id_t)raw;
        if (mod_lfo_dest_supported_fast(track, param, family, type, ctx) == 0U)
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

static uint16_t mod_lfo_dest_count_supported(uint8_t track)
{
    mod_lfo_dest_cache_t *const cache = mod_lfo_dest_cache_resolve(track);
    return (cache != NULL) ? cache->count : 1U;
}

static param_id_t mod_lfo_dest_from_index(uint8_t track, uint16_t dest_index)
{
    if ((track >= SEQ_TRACK_COUNT) || (dest_index == 0U))
    {
        return MOD_LFO_DEST_NONE;
    }

    mod_lfo_dest_cache_t *const cache = mod_lfo_dest_cache_resolve(track);
    if ((cache == NULL) || (dest_index >= cache->count))
    {
        return MOD_LFO_DEST_NONE;
    }

    return cache->index_to_param[dest_index];
}

static uint16_t mod_lfo_dest_to_index(uint8_t track, param_id_t dest)
{
    if ((track >= SEQ_TRACK_COUNT) || (dest >= PARAM_COUNT))
    {
        return 0U;
    }

    mod_lfo_dest_cache_t *const cache = mod_lfo_dest_cache_resolve(track);
    if (cache == NULL)
    {
        return 0U;
    }

    return cache->param_to_index[(uint16_t)dest];
}

static uint32_t mod_lfo_phase_inc_from_hz(float hz)
{
    hz = mod_lfo_clampf(hz, 0.0f, 12000.0f);
    if (hz <= MOD_LFO_RATE_OFF_EPS)
    {
        return 0U;
    }
    const double phase_f = (double)hz * (4294967296.0 * (double)MOD_LFO_PHASE_DT);
    if (phase_f <= 1.0)
    {
        return 1U;
    }
    if (phase_f >= 4294967295.0)
    {
        return 0xFFFFFFFFU;
    }
    return (uint32_t)(phase_f + 0.5);
}

static float mod_lfo_quantize_sync_rate(float rate)
{
    if (rate <= MOD_LFO_RATE_OFF_EPS)
    {
        return rate;
    }

    uint8_t sync = (uint8_t)(rate + 0.5f);
    if (sync == 0U)
    {
        sync = 1U;
    }
    if (sync > MOD_LFO_SYNC_RATE_COUNT)
    {
        sync = MOD_LFO_SYNC_RATE_COUNT;
    }
    return (float)sync;
}

static uint32_t mod_lfo_phase_inc_from_rate_with_bpm(float rate, uint32_t bpm_milli)
{
    if (rate < -MOD_LFO_RATE_OFF_EPS)
    {
        return mod_lfo_phase_inc_from_hz(-rate);
    }
    if (rate > MOD_LFO_RATE_OFF_EPS)
    {
        uint8_t idx = (uint8_t)mod_lfo_quantize_sync_rate(rate);
        idx--;
        if (idx >= MOD_LFO_SYNC_RATE_COUNT)
        {
            idx = MOD_LFO_SYNC_RATE_COUNT - 1U;
        }
        const float bpm = (float)bpm_milli * 0.001f;
        const float bars_per_cycle = g_mod_lfo_sync_bars_per_cycle[idx];
        const float seconds_per_cycle = bars_per_cycle * (240.0f / mod_lfo_clampf(bpm, 40.0f, 300.0f));
        return mod_lfo_phase_inc_from_hz(1.0f / mod_lfo_clampf(seconds_per_cycle, 0.0005f, 60.0f));
    }
    return 0U;
}

static uint32_t mod_lfo_phase_inc_from_rate(float rate)
{
    return mod_lfo_phase_inc_from_rate_with_bpm(rate, seq_runtime_get_tempo_bpm_milli());
}

static uint32_t mod_lfo_seconds_to_frames(float seconds)
{
    if (seconds <= 0.0f)
    {
        return 0U;
    }
    if (seconds > 60.0f)
    {
        seconds = 60.0f;
    }
    return (uint32_t)((seconds * MOD_LFO_AUDIO_SAMPLE_RATE) + 0.5f);
}

static uint32_t mod_lfo_phase_from_degrees(float degrees)
{
    degrees = mod_lfo_clampf(degrees, 0.0f, 360.0f);
    if (degrees >= 360.0f)
    {
        return 0U;
    }
    return (uint32_t)(((double)degrees / 360.0) * 4294967296.0);
}

static void mod_lfo_start_phase(mod_lfo_runtime_state_t *rt, mod_lfo_shape_t shape, float phase_slew)
{
    if (rt == NULL)
    {
        return;
    }

    if (shape == MOD_LFO_SHAPE_RANDOM_SH)
    {
        rt->phase = 0U;
        rt->sh_valid = 0U;
    }
    else
    {
        rt->phase = mod_lfo_phase_from_degrees(phase_slew);
    }
    rt->slew_valid = 0U;
}

static uint32_t mod_lfo_xorshift32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static float mod_lfo_sh_next_value(uint32_t *state)
{
    uint32_t s = mod_lfo_xorshift32(*state);
    const uint32_t a = s;
    s = mod_lfo_xorshift32(s);
    const uint32_t b = s;
    *state = s;

    const float ua = (float)(a >> 8) * (1.0f / 16777215.0f);
    const float ub = (float)(b >> 8) * (1.0f / 16777215.0f);
    return (ua + ub) - 1.0f;
}

static uint8_t mod_lfo_shape_is_positive(mod_lfo_shape_t shape)
{
    return ((shape == MOD_LFO_SHAPE_SINE_POS)
            || (shape == MOD_LFO_SHAPE_TRIANGLE_POS)
            || (shape == MOD_LFO_SHAPE_SQUARE_POS)) ? 1U : 0U;
}

static float mod_lfo_wave(mod_lfo_shape_t shape, uint32_t phase, mod_lfo_runtime_state_t *state)
{
    switch (shape)
    {
        case MOD_LFO_SHAPE_SINE:
        case MOD_LFO_SHAPE_SINE_POS:
        {
            const uint32_t lut_pos = phase >> 24;
            const uint32_t frac = (phase >> 8) & 0xFFFFU;
            const float y0 = g_mod_lfo_sine_lut[lut_pos];
            const float y1 = g_mod_lfo_sine_lut[lut_pos + 1U];
            const float y = y0 + (y1 - y0) * ((float)frac * (1.0f / 65535.0f));
            return (shape == MOD_LFO_SHAPE_SINE_POS) ? ((y + 1.0f) * 0.5f) : y;
        }

        case MOD_LFO_SHAPE_TRIANGLE:
        case MOD_LFO_SHAPE_TRIANGLE_POS:
        {
            const float p = (float)phase * (1.0f / 4294967296.0f);
            const float y = 1.0f - 4.0f * fabsf(p - 0.5f);
            return (shape == MOD_LFO_SHAPE_TRIANGLE_POS) ? ((y + 1.0f) * 0.5f) : y;
        }

        case MOD_LFO_SHAPE_SAW:
            return ((float)phase * (2.0f / 4294967296.0f)) - 1.0f;

        case MOD_LFO_SHAPE_REVERSE_SAW:
            return 1.0f - ((float)phase * (2.0f / 4294967296.0f));

        case MOD_LFO_SHAPE_SQUARE:
        case MOD_LFO_SHAPE_SQUARE_POS:
            if (shape == MOD_LFO_SHAPE_SQUARE_POS)
            {
                return (phase < 0x80000000U) ? 1.0f : 0.0f;
            }
            return (phase < 0x80000000U) ? 1.0f : -1.0f;

        case MOD_LFO_SHAPE_RANDOM_SH:
            return state->sh_value;

        default:
            return 0.0f;
    }
}

static void mod_lfo_capture_hold_value(mod_lfo_runtime_state_t *rt, mod_lfo_shape_t shape)
{
    if (rt == NULL)
    {
        return;
    }

    if (shape == MOD_LFO_SHAPE_RANDOM_SH)
    {
        rt->sh_value = mod_lfo_sh_next_value(&rt->rng_state);
        rt->sh_valid = 1U;
    }

    rt->hold_value = mod_lfo_wave(shape, rt->phase, rt);
    rt->hold_valid = 1U;
    rt->hold_capture_pending = 0U;
}

static uint8_t mod_lfo_is_effectively_active(uint8_t track,
                                              uint8_t lfo_index,
                                              ui_track_family_t family,
                                              ui_track_type_t type,
                                              const track_runtime_ctx_t *ctx)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK))
    {
        return 0U;
    }

    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    const param_id_t dest = mod_lfo_track_settings_dest(track, lfo_index);
    if ((s == NULL) || (dest == MOD_LFO_DEST_NONE) || (s->depth == 0.0f)
            || (mod_lfo_phase_inc_from_rate(s->rate) == 0U))
    {
        return 0U;
    }

    return mod_lfo_dest_supported_fast(track, dest, family, type, ctx);
}

static void mod_lfo_release_last_destination(uint8_t track,
                                             uint8_t lfo_index,
                                             ui_track_family_t family,
                                             ui_track_type_t type,
                                             const track_runtime_ctx_t *ctx)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK))
    {
        return;
    }

    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];
    const param_id_t previous_dest = (param_id_t)rt->last_dest;

    if ((previous_dest >= PARAM_COUNT) || (rt->base_valid == 0U))
    {
        rt->base_valid = 0U;
        rt->last_dest = (uint16_t)MOD_LFO_DEST_NONE;
        rt->calib_valid = 0U;
        rt->depth_scale = 0.0f;
        return;
    }

    uint8_t other_active_same_dest = 0U;
    for (uint8_t other = 0U; other < MOD_LFO_COUNT_PER_TRACK; ++other)
    {
        if (other == lfo_index)
        {
            continue;
        }
        if (mod_lfo_is_effectively_active(track, other, family, type, ctx) == 0U)
        {
            continue;
        }
        if (mod_lfo_track_settings_dest(track, other) == previous_dest)
        {
            other_active_same_dest = 1U;
            break;
        }
    }

    if ((other_active_same_dest == 0U)
            && (mod_lfo_dest_supported_fast(track, previous_dest, family, type, ctx) != 0U))
    {
        (void)mod_lfo_apply_destination_rt(track, previous_dest, ctx, rt->base_value);
    }

    rt->base_valid = 0U;
    rt->last_dest = (uint16_t)MOD_LFO_DEST_NONE;
    rt->calib_valid = 0U;
    rt->depth_scale = 0.0f;
}

static void mod_lfo_process_control_tick(uint32_t elapsed_frames)
{
    if (param_registry_track_structure_transition_is_global_active() != 0U)
    {
        return;
    }

    const uint32_t bpm_milli = seq_runtime_get_tempo_bpm_milli();
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (param_registry_track_structure_transition_is_track_active(track) != 0U)
        {
            continue;
        }

        track_runtime_refresh_track(track);
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        const ui_track_family_t family = ui_get_track_family(track);
        const ui_track_type_t type = ui_get_track_type(track);

        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo);
            mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo];
            const param_id_t dest = mod_lfo_effective_dest(track, lfo);

            if (s == NULL)
            {
                continue;
            }

            if ((rt->last_dest != (uint16_t)MOD_LFO_DEST_NONE)
                    && (rt->last_dest != (uint16_t)(dest + 0.5f)))
            {
                mod_lfo_release_last_destination(track, lfo, family, type, ctx);
            }

            const float depth = mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_DEPTH);
            if ((dest == MOD_LFO_DEST_NONE) || (depth == 0.0f)
                    || (mod_lfo_dest_supported_fast(track, dest, family, type, ctx) == 0U))
            {
                rt->active = 0U;
                rt->hold_capture_pending = 0U;
                mod_lfo_release_last_destination(track, lfo, family, type, ctx);
                continue;
            }

            if ((rt->base_valid == 0U) || (rt->last_dest != (uint16_t)(dest + 0.5f)))
            {
                /* Query seam: seed the modulation base from the pure value surface only. */
                if (param_registry_get_track_value(dest, track, &rt->base_value) == 0U)
                {
                    continue;
                }
                rt->last_dest = (uint16_t)(dest + 0.5f);
                rt->base_valid = 1U;
            }

            const float rate = mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_RATE);
            const float shape = mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_SHAPE);
            const float trig_f = mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_TRIG);
            const float fade = mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_FADE);
            const float phase_slew = mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_PHASE_SLEW);
            const float delay = mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_DELAY);
            const mod_lfo_shape_t shape_id = (mod_lfo_shape_t)((uint8_t)(shape + 0.5f));
            const mod_lfo_trig_mode_t trig = (mod_lfo_trig_mode_t)((uint8_t)(trig_f + 0.5f));

            rt->phase_inc = mod_lfo_phase_inc_from_rate_with_bpm(rate, bpm_milli);
            if (rt->phase_inc == 0U)
            {
                rt->active = 0U;
                rt->hold_capture_pending = 0U;
                mod_lfo_release_last_destination(track, lfo, family, type, ctx);
                continue;
            }

            if (rt->active == 0U)
            {
                rt->active = 1U;
                rt->delay_remaining_frames = mod_lfo_seconds_to_frames(delay);
                rt->fade_elapsed_frames = 0U;
                rt->one_done = 0U;
                rt->one_running = (trig == MOD_LFO_TRIG_ONE) ? 1U : 0U;
                rt->hold_capture_pending = 0U;
                mod_lfo_start_phase(rt, shape_id, phase_slew);
                if (trig == MOD_LFO_TRIG_HOLD)
                {
                    if (rt->delay_remaining_frames > 0U)
                    {
                        rt->hold_capture_pending = 1U;
                    }
                    else
                    {
                        mod_lfo_capture_hold_value(rt, shape_id);
                    }
                }
            }

            if ((trig == MOD_LFO_TRIG_ONE) && (rt->one_done != 0U))
            {
                mod_lfo_release_last_destination(track, lfo, family, type, ctx);
                continue;
            }

            if (rt->delay_remaining_frames > 0U)
            {
                if (rt->delay_remaining_frames > elapsed_frames)
                {
                    rt->delay_remaining_frames -= elapsed_frames;
                    mod_lfo_release_last_destination(track, lfo, family, type, ctx);
                    continue;
                }
                rt->delay_remaining_frames = 0U;
                rt->fade_elapsed_frames = 0U;
                if ((trig == MOD_LFO_TRIG_HOLD) && (rt->hold_capture_pending != 0U))
                {
                    mod_lfo_capture_hold_value(rt, shape_id);
                }
            }

            const uint32_t phase_prev = rt->phase;
            rt->phase += (uint32_t)(((uint64_t)rt->phase_inc) * (uint64_t)elapsed_frames);

            if (shape_id == MOD_LFO_SHAPE_RANDOM_SH)
            {
                if ((rt->sh_valid == 0U) || (rt->phase < phase_prev))
                {
                    rt->sh_value = mod_lfo_sh_next_value(&rt->rng_state);
                    rt->sh_valid = 1U;
                }
            }

            if ((trig == MOD_LFO_TRIG_ONE) && (rt->phase < phase_prev))
            {
                rt->one_done = 1U;
            }

            if ((trig == MOD_LFO_TRIG_HOLD) && (rt->hold_valid != 0U))
            {
                rt->current = rt->hold_value;
            }
            else
            {
                rt->current = mod_lfo_wave(shape_id, phase_prev, rt);
            }

            if (shape_id == MOD_LFO_SHAPE_RANDOM_SH)
            {
                const float slew_norm = mod_lfo_clampf(phase_slew / 360.0f, 0.0f, 1.0f);
                const float coeff = 1.0f - (slew_norm * 0.95f);
                if (rt->slew_valid == 0U)
                {
                    rt->slew_value = rt->current;
                    rt->slew_valid = 1U;
                }
                else
                {
                    rt->slew_value += (rt->current - rt->slew_value) * coeff;
                }
                rt->current = rt->slew_value;
            }

            if ((rt->calib_valid == 0U) || (rt->last_dest != (uint16_t)(dest + 0.5f)))
            {
                const param_desc_t *const desc = &param_registry[dest];
                rt->dest_min = desc->min;
                rt->dest_max = desc->max;
                rt->calib_valid = 1U;
            }
            float amp = 1.0f;
            const uint32_t fade_frames = mod_lfo_seconds_to_frames(fabsf(fade));
            if (fade_frames > 0U)
            {
                rt->fade_elapsed_frames += elapsed_frames;
                if (rt->fade_elapsed_frames > fade_frames)
                {
                    rt->fade_elapsed_frames = fade_frames;
                }
                const float pos = (float)rt->fade_elapsed_frames / (float)fade_frames;
                amp = (fade < 0.0f) ? pos : (1.0f - pos);
            }
            rt->depth_scale = (depth / 127.0f) * (rt->dest_max - rt->dest_min) * amp;
            const float modulated = mod_lfo_clampf(rt->base_value + (rt->current * rt->depth_scale), rt->dest_min, rt->dest_max);
            (void)mod_lfo_apply_destination_rt(track, dest, ctx, modulated);
        }
    }
}

void mod_lfo_v1_init(void)
{
    memset(g_mod_lfo_runtime, 0, sizeof(g_mod_lfo_runtime));
    memset(g_mod_lfo_dest_cache, 0, sizeof(g_mod_lfo_dest_cache));
    memset(g_mod_lfo_midi_cc_cache, 0, sizeof(g_mod_lfo_midi_cc_cache));
    g_mod_lfo_control_counter = 0U;

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            g_mod_lfo_runtime[track][lfo].phase = 0U;
            g_mod_lfo_runtime[track][lfo].phase_inc = 1U;
            g_mod_lfo_runtime[track][lfo].delay_remaining_frames = 0U;
            g_mod_lfo_runtime[track][lfo].fade_elapsed_frames = 0U;
            g_mod_lfo_runtime[track][lfo].current = 0.0f;
            g_mod_lfo_runtime[track][lfo].hold_value = 0.0f;
            g_mod_lfo_runtime[track][lfo].slew_value = 0.0f;
            g_mod_lfo_runtime[track][lfo].rng_state = 0xA341316CU ^ ((uint32_t)track << 8) ^ (uint32_t)lfo;
            g_mod_lfo_runtime[track][lfo].sh_value = 0.0f;
            g_mod_lfo_runtime[track][lfo].sh_valid = 0U;
            g_mod_lfo_runtime[track][lfo].triggered = 0U;
            g_mod_lfo_runtime[track][lfo].one_running = 0U;
            g_mod_lfo_runtime[track][lfo].one_done = 0U;
            g_mod_lfo_runtime[track][lfo].hold_valid = 0U;
            g_mod_lfo_runtime[track][lfo].hold_capture_pending = 0U;
            g_mod_lfo_runtime[track][lfo].slew_valid = 0U;
            g_mod_lfo_runtime[track][lfo].active = 0U;
            g_mod_lfo_runtime[track][lfo].last_dest = (uint16_t)MOD_LFO_DEST_NONE;
            g_mod_lfo_runtime[track][lfo].base_valid = 0U;
            g_mod_lfo_runtime[track][lfo].base_value = 0.0f;
            g_mod_lfo_runtime[track][lfo].dest_min = 0.0f;
            g_mod_lfo_runtime[track][lfo].dest_max = 127.0f;
            g_mod_lfo_runtime[track][lfo].depth_scale = 0.0f;
            g_mod_lfo_runtime[track][lfo].calib_valid = 0U;
            g_mod_lfo_runtime[track][lfo].temp_valid_mask = 0U;
            g_mod_lfo_runtime[track][lfo].temp = (track_mod_lfo_state_t){0};
        }
    }

    mod_lfo_v1_invalidate_dest_cache_all();
}

void mod_lfo_v1_reset_runtime(void)
{
    g_mod_lfo_control_counter = 0U;
    memset(g_mod_lfo_midi_cc_cache, 0, sizeof(g_mod_lfo_midi_cc_cache));
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            g_mod_lfo_runtime[track][lfo].phase = 0U;
            {
                const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo);
                const float rate = (s != NULL) ? s->rate : 0.0f;
                g_mod_lfo_runtime[track][lfo].phase_inc = mod_lfo_phase_inc_from_rate(rate);
            }
            g_mod_lfo_runtime[track][lfo].current = 0.0f;
            g_mod_lfo_runtime[track][lfo].sh_valid = 0U;
            g_mod_lfo_runtime[track][lfo].triggered = 0U;
            g_mod_lfo_runtime[track][lfo].one_running = 0U;
            g_mod_lfo_runtime[track][lfo].one_done = 0U;
            g_mod_lfo_runtime[track][lfo].hold_valid = 0U;
            g_mod_lfo_runtime[track][lfo].hold_capture_pending = 0U;
            g_mod_lfo_runtime[track][lfo].slew_valid = 0U;
            g_mod_lfo_runtime[track][lfo].active = 0U;
            g_mod_lfo_runtime[track][lfo].delay_remaining_frames = 0U;
            g_mod_lfo_runtime[track][lfo].fade_elapsed_frames = 0U;
            g_mod_lfo_runtime[track][lfo].base_valid = 0U;
            g_mod_lfo_runtime[track][lfo].last_dest = (uint16_t)MOD_LFO_DEST_NONE;
            g_mod_lfo_runtime[track][lfo].depth_scale = 0.0f;
            g_mod_lfo_runtime[track][lfo].calib_valid = 0U;
            g_mod_lfo_runtime[track][lfo].temp_valid_mask = 0U;
        }
    }

    mod_lfo_v1_invalidate_dest_cache_all();
}

uint8_t mod_lfo_v1_set_track_param(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return 0U;
    }

    track_mod_lfo_state_t *const s = mod_lfo_track_settings_mut(track, lfo_index);
    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];

    if (s == NULL)
    {
        return 0U;
    }

    switch (param)
    {
        case MOD_LFO_PARAM_DEST:
        {
            rt->temp_valid_mask = 0U;
            track_runtime_refresh_track(track);
            mod_lfo_release_last_destination(track, lfo_index, ui_get_track_family(track), ui_get_track_type(track), track_runtime_get_ctx(track));
            const uint16_t max_index = (uint16_t)(mod_lfo_dest_count_supported(track) - 1U);
            const uint16_t dest_index = (uint16_t)mod_lfo_clampf(value, 0.0f, (float)max_index);
            s->dest = (float)mod_lfo_dest_from_index(track, dest_index);
            rt->base_valid = 0U;
            rt->last_dest = (uint16_t)MOD_LFO_DEST_NONE;
            rt->calib_valid = 0U;
            rt->depth_scale = 0.0f;
            rt->active = 0U;
            rt->hold_capture_pending = 0U;
            return 1U;
        }

        case MOD_LFO_PARAM_RATE:
            rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
            s->rate = mod_lfo_clampf(value, -12.0f, (float)MOD_LFO_SYNC_RATE_COUNT);
            if (s->rate > 0.0f)
            {
                s->rate = mod_lfo_quantize_sync_rate(s->rate);
            }
            rt->phase_inc = mod_lfo_phase_inc_from_rate(s->rate);
            if (rt->phase_inc == 0U)
            {
                rt->active = 0U;
                rt->hold_capture_pending = 0U;
            }
            return 1U;

        case MOD_LFO_PARAM_DEPTH:
            rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
            s->depth = mod_lfo_clampf(value, 0.0f, 127.0f);
            if (s->depth == 0.0f)
            {
                track_runtime_refresh_track(track);
                rt->active = 0U;
                rt->hold_capture_pending = 0U;
                mod_lfo_release_last_destination(track, lfo_index, ui_get_track_family(track), ui_get_track_type(track), track_runtime_get_ctx(track));
            }
            return 1U;

        case MOD_LFO_PARAM_SHAPE:
            rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
            s->shape = mod_lfo_clampf(value, 0.0f, (float)((uint8_t)MOD_LFO_SHAPE_COUNT - 1U));
            rt->sh_valid = 0U;
            rt->slew_valid = 0U;
            return 1U;

        case MOD_LFO_PARAM_DELAY:
            rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
            s->delay = mod_lfo_clampf(value, 0.0f, 10.0f);
            return 1U;

        case MOD_LFO_PARAM_TRIG:
            rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
            s->trig = mod_lfo_clampf(value, 0.0f, (float)((uint8_t)MOD_LFO_TRIG_COUNT - 1U));
            rt->triggered = 0U;
            rt->one_running = 0U;
            rt->one_done = 0U;
            rt->hold_valid = 0U;
            rt->hold_capture_pending = 0U;
            rt->active = 0U;
            rt->fade_elapsed_frames = 0U;
            return 1U;

        case MOD_LFO_PARAM_FADE:
            rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
            s->fade = mod_lfo_clampf(value, -10.0f, 10.0f);
            rt->fade_elapsed_frames = 0U;
            return 1U;

        case MOD_LFO_PARAM_PHASE_SLEW:
            rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
            s->phase_slew = mod_lfo_clampf(value, 0.0f, 360.0f);
            rt->slew_valid = 0U;
            return 1U;

        default:
            return 0U;
    }
}

uint8_t mod_lfo_v1_apply_track_param_temp(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return 0U;
    }

    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];
    if (s == NULL)
    {
        return 0U;
    }

    if (rt->temp_valid_mask == 0U)
    {
        rt->temp = *s;
    }

    switch (param)
    {
        case MOD_LFO_PARAM_DEST:
        {
            track_runtime_refresh_track(track);
            mod_lfo_release_last_destination(track, lfo_index, ui_get_track_family(track), ui_get_track_type(track), track_runtime_get_ctx(track));
            const uint16_t max_index = (uint16_t)(mod_lfo_dest_count_supported(track) - 1U);
            const uint16_t dest_index = (uint16_t)mod_lfo_clampf(value, 0.0f, (float)max_index);
            rt->temp.dest = (float)mod_lfo_dest_from_index(track, dest_index);
            rt->base_valid = 0U;
            rt->last_dest = (uint16_t)MOD_LFO_DEST_NONE;
            rt->calib_valid = 0U;
            rt->depth_scale = 0.0f;
            rt->active = 0U;
            rt->hold_capture_pending = 0U;
            break;
        }

        case MOD_LFO_PARAM_RATE:
            rt->temp.rate = mod_lfo_clampf(value, -12.0f, (float)MOD_LFO_SYNC_RATE_COUNT);
            if (rt->temp.rate > 0.0f)
            {
                rt->temp.rate = mod_lfo_quantize_sync_rate(rt->temp.rate);
            }
            rt->phase_inc = mod_lfo_phase_inc_from_rate(rt->temp.rate);
            if (rt->phase_inc == 0U)
            {
                rt->active = 0U;
                rt->hold_capture_pending = 0U;
            }
            break;

        case MOD_LFO_PARAM_DEPTH:
            rt->temp.depth = mod_lfo_clampf(value, 0.0f, 127.0f);
            if (rt->temp.depth == 0.0f)
            {
                track_runtime_refresh_track(track);
                rt->active = 0U;
                rt->hold_capture_pending = 0U;
                mod_lfo_release_last_destination(track, lfo_index, ui_get_track_family(track), ui_get_track_type(track), track_runtime_get_ctx(track));
            }
            break;

        case MOD_LFO_PARAM_SHAPE:
            rt->temp.shape = mod_lfo_clampf(value, 0.0f, (float)((uint8_t)MOD_LFO_SHAPE_COUNT - 1U));
            rt->sh_valid = 0U;
            rt->slew_valid = 0U;
            break;

        case MOD_LFO_PARAM_DELAY:
            rt->temp.delay = mod_lfo_clampf(value, 0.0f, 10.0f);
            break;

        case MOD_LFO_PARAM_TRIG:
            rt->temp.trig = mod_lfo_clampf(value, 0.0f, (float)((uint8_t)MOD_LFO_TRIG_COUNT - 1U));
            rt->triggered = 0U;
            rt->one_running = 0U;
            rt->one_done = 0U;
            rt->hold_valid = 0U;
            rt->hold_capture_pending = 0U;
            rt->active = 0U;
            rt->fade_elapsed_frames = 0U;
            break;

        case MOD_LFO_PARAM_FADE:
            rt->temp.fade = mod_lfo_clampf(value, -10.0f, 10.0f);
            rt->fade_elapsed_frames = 0U;
            break;

        case MOD_LFO_PARAM_PHASE_SLEW:
            rt->temp.phase_slew = mod_lfo_clampf(value, 0.0f, 360.0f);
            rt->slew_valid = 0U;
            break;

        default:
            return 0U;
    }

    rt->temp_valid_mask |= mod_lfo_runtime_param_mask(param);
    return 1U;
}

void mod_lfo_v1_clear_track_param_temp(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return;
    }

    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];
    rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
    if (param == MOD_LFO_PARAM_DEST)
    {
        rt->base_valid = 0U;
        rt->last_dest = (uint16_t)MOD_LFO_DEST_NONE;
        rt->calib_valid = 0U;
        rt->depth_scale = 0.0f;
        rt->active = 0U;
        rt->hold_capture_pending = 0U;
    }
}

uint8_t mod_lfo_v1_get_track_param(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float *out_value)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || (out_value == NULL)
            || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return 0U;
    }

    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    if (s == NULL)
    {
        return 0U;
    }

    switch (param)
    {
        case MOD_LFO_PARAM_DEST:
            *out_value = (float)mod_lfo_dest_to_index(track, (param_id_t)((uint16_t)(s->dest + 0.5f)));
            return 1U;

        case MOD_LFO_PARAM_RATE:
            *out_value = s->rate;
            return 1U;

        case MOD_LFO_PARAM_DEPTH:
            *out_value = s->depth;
            return 1U;

        case MOD_LFO_PARAM_SHAPE:
            *out_value = s->shape;
            return 1U;

        case MOD_LFO_PARAM_DELAY:
            *out_value = s->delay;
            return 1U;

        case MOD_LFO_PARAM_TRIG:
            *out_value = s->trig;
            return 1U;

        case MOD_LFO_PARAM_FADE:
            *out_value = s->fade;
            return 1U;

        case MOD_LFO_PARAM_PHASE_SLEW:
            *out_value = s->phase_slew;
            return 1U;

        default:
            return 0U;
    }
}

void mod_lfo_v1_resync_base_on_authoritative_write(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    uint8_t midi_cc_index = 0U;
    if (mod_lfo_midi_cc_cache_index(id, &midi_cc_index) != 0U)
    {
        g_mod_lfo_midi_cc_cache[track][midi_cc_index].valid = 0U;
    }

    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo);
        mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo];
        if ((s == NULL) || ((param_id_t)((uint16_t)(s->dest + 0.5f)) != id))
        {
            continue;
        }
        if (((param_id_t)rt->last_dest != id) || (rt->base_valid == 0U))
        {
            continue;
        }

        rt->base_value = value;
    }
}

void mod_lfo_v1_process_sample_all(void)
{
    mod_lfo_v1_process_block(1U);
}

void mod_lfo_v1_process_block(uint32_t frames)
{
    if (frames == 0U)
    {
        return;
    }

#if MOD_LFO_WINDOW_RATE_EXPERIMENT
    g_mod_lfo_control_counter = 0U;
    mod_lfo_process_control_tick(frames);
#else
    g_mod_lfo_control_counter += frames;
    while (g_mod_lfo_control_counter >= MOD_LFO_CONTROL_STRIDE)
    {
        g_mod_lfo_control_counter -= MOD_LFO_CONTROL_STRIDE;
        mod_lfo_process_control_tick(1U);
    }
#endif
}

void mod_lfo_v1_note_trigger(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo);
        mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo];
        if (s == NULL)
        {
            continue;
        }

        const mod_lfo_trig_mode_t trig = (mod_lfo_trig_mode_t)((uint8_t)(s->trig + 0.5f));
        const mod_lfo_shape_t shape = (mod_lfo_shape_t)((uint8_t)(s->shape + 0.5f));
        if (trig == MOD_LFO_TRIG_FREE)
        {
            continue;
        }

        rt->triggered = 1U;
        rt->delay_remaining_frames = mod_lfo_seconds_to_frames(s->delay);
        rt->fade_elapsed_frames = 0U;
        rt->one_done = 0U;
        rt->one_running = (trig == MOD_LFO_TRIG_ONE) ? 1U : 0U;

        if ((trig == MOD_LFO_TRIG_TRIG) || (trig == MOD_LFO_TRIG_ONE))
        {
            mod_lfo_start_phase(rt, shape, s->phase_slew);
        }
        else if (trig == MOD_LFO_TRIG_HOLD)
        {
            if (rt->delay_remaining_frames > 0U)
            {
                rt->hold_capture_pending = 1U;
            }
            else
            {
                mod_lfo_capture_hold_value(rt, shape);
            }
        }
    }
}

uint8_t mod_lfo_v1_shape_is_random(uint8_t track, uint8_t lfo_index)
{
    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    if (s == NULL)
    {
        return 0U;
    }
    return ((uint8_t)(s->shape + 0.5f) == (uint8_t)MOD_LFO_SHAPE_RANDOM_SH) ? 1U : 0U;
}

uint8_t mod_lfo_v1_waveform_point(uint8_t track, uint8_t lfo_index, uint8_t x, uint8_t width, int8_t *out_y_q7)
{
    if ((out_y_q7 == NULL) || (width == 0U))
    {
        return 0U;
    }

    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    if (s == NULL)
    {
        return 0U;
    }

    const mod_lfo_shape_t shape = (mod_lfo_shape_t)((uint8_t)(s->shape + 0.5f));
    if (shape == MOD_LFO_SHAPE_RANDOM_SH)
    {
        const int8_t rnd_pattern[8] = {-48, 32, 84, -16, -80, 4, 56, -28};
        *out_y_q7 = rnd_pattern[(width > 1U) ? ((x * 8U) / width) & 7U : 0U];
        return 1U;
    }

    mod_lfo_runtime_state_t preview = {0};
    preview.sh_value = 0.0f;
    const uint32_t phase = (uint32_t)(((uint64_t)x * 4294967296ULL) / (uint64_t)width);
    float y = mod_lfo_wave(shape, phase, &preview);
    if (mod_lfo_shape_is_positive(shape) != 0U)
    {
        y = (y * 2.0f) - 1.0f;
    }
    y = mod_lfo_clampf(y, -1.0f, 1.0f);
    *out_y_q7 = (int8_t)(y * 63.0f);
    return 1U;
}

uint16_t mod_lfo_v1_dest_count(uint8_t track)
{
    return mod_lfo_dest_count_supported(track);
}

uint8_t mod_lfo_v1_dest_param_at(uint8_t track, uint16_t dest_index, param_id_t *out_param)
{
    if (out_param == NULL)
    {
        return 0U;
    }

    *out_param = mod_lfo_dest_from_index(track, dest_index);
    return 1U;
}

uint8_t mod_lfo_v1_dest_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return 0U;
    }

    const param_id_t dest = mod_lfo_dest_from_index(track, dest_index);
    if (dest == MOD_LFO_DEST_NONE)
    {
        out[0] = 'N';
        out[1] = 'o';
        out[2] = 'n';
        out[3] = 'e';
        out[4] = '\0';
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

static const char *mod_lfo_short_label_for_param(param_id_t dest)
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

static void mod_lfo_copy_short_label(const char *src, char *out, uint32_t out_len)
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

uint8_t mod_lfo_v1_dest_short_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return 0U;
    }

    const param_id_t dest = mod_lfo_dest_from_index(track, dest_index);
    if (dest == MOD_LFO_DEST_NONE)
    {
        mod_lfo_copy_short_label("None", out, out_len);
        return 1U;
    }

    if (dest >= PARAM_COUNT)
    {
        return 0U;
    }

    const char *label = NULL;
    if (param_wave_label_for_track_param(track, dest, &label) == 0U)
    {
        label = mod_lfo_short_label_for_param(dest);
    }
    if (label == NULL)
    {
        label = param_registry[dest].name;
    }
    if (label == NULL)
    {
        return 0U;
    }

    mod_lfo_copy_short_label(label, out, out_len);
    return 1U;
}
