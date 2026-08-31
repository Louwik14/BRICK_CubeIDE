#include "Param/param_filter_audio.h"

#include <math.h>
#include <stddef.h>

#include "Audio/audio_note_engine_adapter.h"
#include "Audio/mixer.h"

static float filter_audio_clamp(float value, float lo, float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static float filter_audio_unit(float value)
{
    return filter_audio_clamp(value, 0.0f, 127.0f) * (1.0f / 127.0f);
}

static float filter_audio_time(float value)
{
    return 0.001f * exp2f(12.287712379549449f * filter_audio_unit(value));
}

uint8_t param_filter_audio_is_param(param_id_t id)
{
    return ((id >= PARAM_FILTER_MORPH) && (id <= PARAM_FILTER_ENVDLY)) ? 1U : 0U;
}

float param_filter_audio_attack_s(float value) { return filter_audio_time(value); }
float param_filter_audio_decay_s(float value) { return filter_audio_time(value); }
float param_filter_audio_sustain(float value) { return filter_audio_unit(value); }
float param_filter_audio_release_s(float value) { return filter_audio_time(value); }
float param_filter_audio_cutoff_hz(float value)
{ return 20.0f * exp2f(9.6438561897747247f * filter_audio_unit(value)); }
float param_filter_audio_resonance(float value) { return filter_audio_unit(value); }
float param_filter_audio_eg_amount(float value) { return filter_audio_unit(value); }
float param_filter_audio_keytrack(float value) { return filter_audio_unit(value); }

uint8_t param_filter_apply_value_audio(param_id_t id,
                                       uint8_t track,
                                       float value)
{
    track_audio_runtime_ctx_t ctx;
    uint8_t target = 0U;
    if ((audio_note_engine_adapter_current_ctx(track, &ctx) == 0U)
            || (audio_note_engine_adapter_ctx_is_audio_routable(&ctx) == 0U)
            || (audio_note_engine_adapter_ctx_filter_target(&ctx, &target) == 0U))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_FILTER_MORPH:
            mixer_set_track_filter_morph(target,
                filter_audio_clamp(value, 0.0f, 127.0f));
            return 1U;
        case PARAM_FILTER_CUTOFF:
            mixer_set_track_filter_cutoff(target,
                param_filter_audio_cutoff_hz(value));
            return 1U;
        case PARAM_FILTER_RESONANCE:
            mixer_set_track_filter_resonance(target, filter_audio_unit(value));
            return 1U;
        case PARAM_FILTER_EG_AMT:
            mixer_set_track_filter_eg_amount(target, filter_audio_unit(value));
            return 1U;
        case PARAM_FILTER_ATTACK:
            mixer_set_track_filter_attack(target, filter_audio_time(value));
            return 1U;
        case PARAM_FILTER_DECAY:
            mixer_set_track_filter_decay(target, filter_audio_time(value));
            return 1U;
        case PARAM_FILTER_SUSTAIN:
            mixer_set_track_filter_sustain(target, filter_audio_unit(value));
            return 1U;
        case PARAM_FILTER_RELEASE:
            mixer_set_track_filter_release(target, filter_audio_time(value));
            return 1U;
        case PARAM_FILTER_KEYTRK:
            mixer_set_track_filter_keytrack(target, filter_audio_unit(value));
            return 1U;
        case PARAM_FILTER_ENVRST:
        case PARAM_FILTER_ENVDLY:
            return 1U;
        default:
            return 0U;
    }
}
