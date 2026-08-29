#include "Audio/drum_synth.h"
#include "Audio/md_model.h"
#include "Audio/md_dsp.h"

#include <cstring>
#include <new>

#include "Track/entity_topology.h"

#include "plaits/dsp/drums/analog_bass_drum.h"
#include "plaits/dsp/engine/engine.h"

namespace
{
constexpr float kDefaultPitch = 0.0f;
constexpr float kDefaultDecay = 0.4f;
constexpr float kDefaultTone = 0.0f;
constexpr float kDefaultFm = 0.3f;
constexpr float kMdSampleRate = 48000.0f;

typedef md_decay_env_t md_internal_env_t;

typedef struct
{
    md_phase_t fundamental;
    md_phase_t harmonic;
    md_internal_env_t amplitude_env;
    md_internal_env_t pitch_env;
    md_internal_env_t transient_env;
    md_internal_env_t noise_env;
    md_rng_t rng;
    md_retrigger_fade_t retrigger_fade;
    float velocity;
    float ramp_ratio;
    float transient_gain;
    float noise_gain;
    float harmonic_mix;
    float drive;
    float last_output;
} md_trx_bd_state_t;

typedef struct
{
    md_phase_t tone_a;
    md_phase_t tone_b;
    md_internal_env_t amplitude_env;
    md_internal_env_t pitch_env;
    md_internal_env_t snap_env;
    md_rng_t rng;
    md_retrigger_fade_t retrigger_fade;
    uint32_t target_increment_a;
    uint32_t target_increment_b;
    float velocity;
    float bump_ratio;
    float tonal_gain;
    float noise_gain;
    float snap_gain;
    float tune_ratio;
    float drive;
    float last_output;
} md_trx_sd_state_t;

typedef struct
{
    md_phase_t oscillator[6];
    md_internal_env_t amplitude_env;
    md_lpf_t lowpass;
    md_hpf_t variable_highpass;
    md_hpf_t fixed_highpass;
    md_retrigger_fade_t retrigger_fade;
    float velocity;
    float secondary_mix;
    float gap_spread_semitones;
    float last_output;
} md_trx_ch_state_t;

typedef struct
{
    md_phase_t carrier;
    md_phase_t modulator;
    uint32_t target_carrier_increment;
    uint32_t target_modulator_increment;
    float previous_modulator;
} md_efm_pm_state_t;

typedef struct
{
    md_efm_pm_state_t pm;
    md_internal_env_t amplitude_env;
    md_internal_env_t pitch_env;
    md_internal_env_t modulation_env;
    md_retrigger_fade_t retrigger_fade;
    float velocity;
    float ramp_ratio;
    float modulation_index;
    float modulator_ratio;
    float feedback;
    float last_output;
} md_efm_bd_state_t;

typedef struct
{
    md_efm_pm_state_t pm;
    md_internal_env_t amplitude_env;
    md_internal_env_t modulation_env;
    md_internal_env_t noise_env;
    md_rng_t rng;
    md_hpf_t highpass;
    md_retrigger_fade_t retrigger_fade;
    float velocity;
    float modulation_index;
    float modulator_ratio;
    float noise_gain;
    float last_output;
} md_efm_sd_state_t;

typedef struct
{
    md_efm_pm_state_t branch_a;
    md_efm_pm_state_t branch_b;
    md_internal_env_t amplitude_env;
    md_internal_env_t snap_env;
    md_internal_env_t modulation_env;
    md_retrigger_fade_t retrigger_fade;
    float velocity;
    float snap_mix;
    float feedback;
    float modulation_index;
    float modulator_ratio;
    float last_output;
} md_efm_cb_state_t;

union drum_engine_state_t
{
    plaits::AnalogBassDrum bd;
    md_trx_bd_state_t trx_bd;
    md_trx_sd_state_t trx_sd;
    md_trx_ch_state_t trx_ch;
    md_efm_bd_state_t efm_bd;
    md_efm_sd_state_t efm_sd;
    md_efm_cb_state_t efm_cb;
    drum_engine_state_t() {}
    ~drum_engine_state_t() {}
};

typedef struct
{
    drum_engine_state_t engine;
    drum_model_id_t model;
    float midi_note;
    float accent;
    float pitch;
    float frequency_current;
    float decay;
    float tone;
    float fm;
    uint8_t initialized;
    uint8_t triggered;
    uint8_t trigger_pending;
    uint8_t md_model;
    uint8_t md_slots[8];
} drum_synth_instance_t;

static drum_synth_instance_t
    g_drum_instances[BRICK_ENTITY_TOP_LEVEL_COUNT];

static inline float clampf_local(float value, float lo, float hi)
{
    if (value < lo)
    {
        return lo;
    }
    if (value > hi)
    {
        return hi;
    }
    return value;
}

static inline drum_synth_instance_t *drum_instance(uint8_t instance_id)
{
    return (instance_id < BRICK_ENTITY_TOP_LEVEL_COUNT)
        ? &g_drum_instances[instance_id] : nullptr;
}

static float md_fast_exp2(float exponent)
{
    int whole = (int)exponent;
    if ((float)whole > exponent)
    {
        --whole;
    }
    const float fraction = exponent - (float)whole;
    const float fraction2 = fraction * fraction;
    float result = 1.0f
        + (0.69314718f * fraction)
        + (0.24022651f * fraction2)
        + (0.05550411f * fraction2 * fraction);
    while (whole > 0)
    {
        result *= 2.0f;
        --whole;
    }
    while (whole < 0)
    {
        result *= 0.5f;
        ++whole;
    }
    return result;
}

static float md_expmap(float lo, float octaves, float normalized)
{
    return lo * md_fast_exp2(octaves * clampf_local(normalized, 0.0f, 1.0f));
}

static void md_internal_env_prepare(md_internal_env_t *env, float seconds)
{
    md_decay_env_prepare(env, seconds, kMdSampleRate);
}

static void md_internal_env_trigger(md_internal_env_t *env, float level)
{
    md_decay_env_trigger(env, level);
}

static float md_internal_env_process(md_internal_env_t *env)
{
    return md_decay_env_process(env);
}

static void md_trx_bd_prepare(drum_synth_instance_t *instance)
{
    md_trx_bd_state_t *const state = &instance->engine.trx_bd;
    const float decay_u = (float)instance->md_slots[1] * (1.0f / 127.0f);
    const float ramp_u = (float)instance->md_slots[2] * (1.0f / 127.0f);
    const float ramp_decay_u = (float)instance->md_slots[3] * (1.0f / 127.0f);
    const float start_u = (float)instance->md_slots[4] * (1.0f / 127.0f);
    const float noise_u = (float)instance->md_slots[5] * (1.0f / 127.0f);
    const float harmonic_u = (float)instance->md_slots[6] * (1.0f / 127.0f);
    const float clip_u = (float)instance->md_slots[7] * (1.0f / 127.0f);

    md_internal_env_prepare(&state->amplitude_env, md_expmap(0.03f, 7.0588937f, decay_u));
    md_internal_env_prepare(&state->pitch_env, md_expmap(0.002f, 8.6438562f, ramp_decay_u));
    md_internal_env_prepare(&state->transient_env, 0.0015f + (0.006f * start_u));
    md_internal_env_prepare(&state->noise_env, 0.003f + (0.025f * noise_u));
    state->ramp_ratio = md_fast_exp2(6.0f * ramp_u);
    state->transient_gain = start_u;
    state->noise_gain = noise_u * noise_u;
    state->harmonic_mix = harmonic_u;
    state->drive = md_fast_exp2(4.0f * clip_u);
}

static void md_trx_bd_reset(drum_synth_instance_t *instance)
{
    md_trx_bd_state_t *const state = &instance->engine.trx_bd;
    std::memset(state, 0, sizeof(*state));
    md_rng_seed(&state->rng, 0x54525842UL ^ (uint32_t)(instance - g_drum_instances));
    md_trx_bd_prepare(instance);
}

static void md_trx_bd_note_on(drum_synth_instance_t *instance, uint8_t midi_note, uint8_t velocity)
{
    md_trx_bd_state_t *const state = &instance->engine.trx_bd;
    const float pitch_u = (float)instance->md_slots[0] * (1.0f / 127.0f);
    const float base_hz = md_expmap(25.0f, 3.5849625f, pitch_u);
    const float note_ratio = md_fast_exp2(((float)midi_note - 36.0f) * (1.0f / 12.0f));
    const float target_hz = clampf_local(base_hz * note_ratio, 20.0f, 1200.0f);

    md_retrigger_fade_begin(&state->retrigger_fade, state->last_output, 32U);
    md_phase_reset(&state->fundamental, 0U);
    md_phase_reset(&state->harmonic, 0U);
    md_phase_set_frequency(&state->fundamental, target_hz, kMdSampleRate);
    state->harmonic.increment = (state->fundamental.increment <= 0x7FFFFFFFUL)
        ? (state->fundamental.increment << 1U)
        : 0xFFFFFFFFUL;
    state->velocity = clampf_local((float)velocity * (1.0f / 127.0f), 0.0f, 1.0f);
    md_rng_seed(&state->rng, 0x54525842UL ^ (uint32_t)(instance - g_drum_instances));
    md_internal_env_trigger(&state->amplitude_env, 1.0f);
    md_internal_env_trigger(&state->pitch_env, 1.0f);
    md_internal_env_trigger(&state->transient_env, state->transient_gain);
    md_internal_env_trigger(&state->noise_env, state->noise_gain);
    instance->triggered = 1U;
}

static void md_trx_bd_render(drum_synth_instance_t *instance, float *mono_out, uint32_t frames)
{
    md_trx_bd_state_t *const state = &instance->engine.trx_bd;
    const uint32_t target_increment = state->fundamental.increment;
    const float pitch_delta = (float)target_increment * (state->ramp_ratio - 1.0f);

    for (uint32_t i = 0U; i < frames; ++i)
    {
        const float pitch_env = md_internal_env_process(&state->pitch_env);
        const float increment_f = (float)target_increment + (pitch_delta * pitch_env);
        const uint32_t increment = (increment_f >= 4294967040.0f)
            ? 0xFFFFFFFFUL
            : (uint32_t)increment_f;
        state->fundamental.increment = increment;
        state->harmonic.increment = (increment <= 0x7FFFFFFFUL)
            ? (increment << 1U)
            : 0xFFFFFFFFUL;

        const float fundamental = md_phase_sine_next(&state->fundamental);
        const float harmonic = md_phase_sine_next(&state->harmonic);
        const float tonal = md_mix2(fundamental, harmonic, state->harmonic_mix * 0.7f);
        const float transient = md_internal_env_process(&state->transient_env);
        const float noise = md_rng_next_bipolar(&state->rng) * md_internal_env_process(&state->noise_env);
        const float amplitude = md_internal_env_process(&state->amplitude_env);
        const float raw = ((tonal * amplitude) + transient + noise) * state->velocity;
        const float output = md_clip(md_retrigger_fade_process(&state->retrigger_fade,
                                                               md_clip(raw, state->drive)),
                                     1.0f);
        mono_out[i] = output;
        state->last_output = output;
    }

    state->fundamental.increment = target_increment;
    if (state->amplitude_env.value == 0.0f)
    {
        instance->triggered = 0U;
        state->last_output = 0.0f;
    }
}

static void md_trx_sd_prepare(drum_synth_instance_t *instance)
{
    md_trx_sd_state_t *const state = &instance->engine.trx_sd;
    const float decay_u = (float)instance->md_slots[1] * (1.0f / 127.0f);
    const float bump_u = (float)instance->md_slots[2] * (1.0f / 127.0f);
    const float bump_env_u = (float)instance->md_slots[3] * (1.0f / 127.0f);
    const float snap_u = (float)instance->md_slots[4] * (1.0f / 127.0f);
    const float tone_u = (float)instance->md_slots[5] * (1.0f / 127.0f);
    const float tune_u = (float)instance->md_slots[6] * (1.0f / 127.0f);
    const float clip_u = (float)instance->md_slots[7] * (1.0f / 127.0f);
    const float tune_semitones = 3.0f + (21.0f * tune_u);

    md_internal_env_prepare(&state->amplitude_env, md_expmap(0.025f, 6.6438562f, decay_u));
    md_internal_env_prepare(&state->pitch_env, md_expmap(0.002f, 6.9657843f, bump_env_u));
    md_internal_env_prepare(&state->snap_env, 0.012f);
    state->bump_ratio = md_fast_exp2(4.0f * bump_u);
    state->snap_gain = snap_u * snap_u;
    state->tonal_gain = 0.35f + (0.65f * tone_u);
    state->noise_gain = 1.0f - (0.70f * tone_u);
    state->tune_ratio = md_fast_exp2(tune_semitones * (1.0f / 12.0f));
    state->drive = md_fast_exp2(4.0f * clip_u);
}

static void md_trx_sd_reset(drum_synth_instance_t *instance)
{
    md_trx_sd_state_t *const state = &instance->engine.trx_sd;
    std::memset(state, 0, sizeof(*state));
    md_rng_seed(&state->rng, 0x54525853UL ^ (uint32_t)(instance - g_drum_instances));
    md_trx_sd_prepare(instance);
}

static void md_trx_sd_note_on(drum_synth_instance_t *instance, uint8_t midi_note, uint8_t velocity)
{
    md_trx_sd_state_t *const state = &instance->engine.trx_sd;
    const float pitch_u = (float)instance->md_slots[0] * (1.0f / 127.0f);
    const float base_hz = md_expmap(70.0f, 2.5849625f, pitch_u);
    const float note_ratio = md_fast_exp2(((float)midi_note - 36.0f) * (1.0f / 12.0f));
    const float target_hz = clampf_local(base_hz * note_ratio, 20.0f, 6000.0f);

    md_retrigger_fade_begin(&state->retrigger_fade, state->last_output, 32U);
    md_phase_reset(&state->tone_a, 0U);
    md_phase_reset(&state->tone_b, 0U);
    state->target_increment_a = md_phase_increment_from_hz(target_hz, kMdSampleRate);
    state->target_increment_b = md_phase_increment_from_hz(target_hz * state->tune_ratio,
                                                            kMdSampleRate);
    state->velocity = clampf_local((float)velocity * (1.0f / 127.0f), 0.0f, 1.0f);
    md_rng_seed(&state->rng, 0x54525853UL ^ (uint32_t)(instance - g_drum_instances));
    md_internal_env_trigger(&state->amplitude_env, 1.0f);
    md_internal_env_trigger(&state->pitch_env, 1.0f);
    md_internal_env_trigger(&state->snap_env, state->snap_gain);
    instance->triggered = 1U;
}

static void md_trx_sd_render(drum_synth_instance_t *instance, float *mono_out, uint32_t frames)
{
    md_trx_sd_state_t *const state = &instance->engine.trx_sd;
    const float pitch_scale = state->bump_ratio - 1.0f;

    for (uint32_t i = 0U; i < frames; ++i)
    {
        const float pitch_ratio = 1.0f + (pitch_scale * md_internal_env_process(&state->pitch_env));
        const float increment_a = (float)state->target_increment_a * pitch_ratio;
        const float increment_b = (float)state->target_increment_b * pitch_ratio;
        state->tone_a.increment = (increment_a >= 4294967040.0f)
            ? 0xFFFFFFFFUL : (uint32_t)increment_a;
        state->tone_b.increment = (increment_b >= 4294967040.0f)
            ? 0xFFFFFFFFUL : (uint32_t)increment_b;

        const float tone_a = md_phase_sine_next(&state->tone_a);
        const float tone_b = md_phase_sine_next(&state->tone_b);
        const float tonal = ((0.62f * tone_a) + (0.38f * tone_b))
            * md_internal_env_process(&state->amplitude_env) * state->tonal_gain;
        const float noise = md_rng_next_bipolar(&state->rng)
            * md_internal_env_process(&state->snap_env) * state->noise_gain;
        const float raw = (tonal + noise) * state->velocity;
        const float output = md_clip(md_retrigger_fade_process(&state->retrigger_fade,
                                                               md_clip(raw, state->drive)),
                                     1.0f);
        mono_out[i] = output;
        state->last_output = output;
    }

    if (state->amplitude_env.value == 0.0f)
    {
        instance->triggered = 0U;
        state->last_output = 0.0f;
    }
}

static float md_filter_pole_from_hz(float cutoff_hz)
{
    return md_fast_exp2(-9.0647203f * cutoff_hz / kMdSampleRate);
}

static void md_trx_ch_prepare(drum_synth_instance_t *instance)
{
    md_trx_ch_state_t *const state = &instance->engine.trx_ch;
    const float decay_u = (float)instance->md_slots[1] * (1.0f / 127.0f);
    const float highpass_u = (float)instance->md_slots[2] * (1.0f / 127.0f);
    const float lowpass_u = (float)instance->md_slots[3] * (1.0f / 127.0f);
    const float metal_u = (float)instance->md_slots[4] * (1.0f / 127.0f);
    const float gap_signed = (instance->md_slots[0] <= 64U)
        ? ((float)instance->md_slots[0] - 64.0f) * (1.0f / 64.0f)
        : ((float)instance->md_slots[0] - 64.0f) * (1.0f / 63.0f);
    const float highpass_hz = md_expmap(200.0f, 6.1292830f, highpass_u);
    const float lowpass_hz = md_expmap(1200.0f, 4.0588937f, lowpass_u);

    md_internal_env_prepare(&state->amplitude_env, md_expmap(0.008f, 7.5507468f, decay_u));
    md_lpf_prepare(&state->lowpass, 1.0f - md_filter_pole_from_hz(lowpass_hz));
    md_hpf_prepare(&state->variable_highpass, md_filter_pole_from_hz(highpass_hz));
    md_hpf_prepare(&state->fixed_highpass, md_filter_pole_from_hz(30.0f));
    state->gap_spread_semitones = gap_signed * 4.0f;
    state->secondary_mix = 0.15f + (0.85f * metal_u);
}

static void md_trx_ch_reset(drum_synth_instance_t *instance)
{
    md_trx_ch_state_t *const state = &instance->engine.trx_ch;
    std::memset(state, 0, sizeof(*state));
    md_trx_ch_prepare(instance);
}

static void md_trx_ch_note_on(drum_synth_instance_t *instance, uint8_t midi_note, uint8_t velocity)
{
    static const uint8_t periods[6] = { 114U, 102U, 86U, 80U, 58U, 52U };
    static const float spread_position[6] = {
        -1.0f, -0.6f, -0.2f, 0.2f, 0.6f, 1.0f
    };
    md_trx_ch_state_t *const state = &instance->engine.trx_ch;
    const float note_ratio = md_fast_exp2(((float)midi_note - 36.0f) * (1.0f / 12.0f));

    md_retrigger_fade_begin(&state->retrigger_fade, state->last_output, 32U);
    for (uint8_t oscillator = 0U; oscillator < 6U; ++oscillator)
    {
        const float gap_ratio = md_fast_exp2(spread_position[oscillator]
                                             * state->gap_spread_semitones
                                             * (1.0f / 12.0f));
        const float frequency_hz = (kMdSampleRate / (float)periods[oscillator])
            * note_ratio * gap_ratio;
        md_phase_reset(&state->oscillator[oscillator], 0U);
        md_phase_set_frequency(&state->oscillator[oscillator], frequency_hz, kMdSampleRate);
    }
    md_lpf_reset(&state->lowpass);
    md_hpf_reset(&state->variable_highpass);
    md_hpf_reset(&state->fixed_highpass);
    state->velocity = clampf_local((float)velocity * (1.0f / 127.0f), 0.0f, 1.0f);
    md_internal_env_trigger(&state->amplitude_env, 1.0f);
    instance->triggered = 1U;
}

static void md_trx_ch_render(drum_synth_instance_t *instance, float *mono_out, uint32_t frames)
{
    md_trx_ch_state_t *const state = &instance->engine.trx_ch;
    const float normalization = 1.0f / (3.0f * (1.0f + state->secondary_mix));

    for (uint32_t i = 0U; i < frames; ++i)
    {
        float primary = 0.0f;
        float secondary = 0.0f;
        for (uint8_t oscillator = 0U; oscillator < 3U; ++oscillator)
        {
            primary += md_phase_square_next(&state->oscillator[oscillator]);
            secondary += md_phase_square_next(&state->oscillator[oscillator + 3U]);
        }
        const float metallic = (primary + (secondary * state->secondary_mix)) * normalization;
        const float lowpassed = md_lpf_process(&state->lowpass, metallic);
        const float highpassed = md_hpf_process(&state->variable_highpass, lowpassed);
        const float dc_free = md_hpf_process(&state->fixed_highpass, highpassed);
        const float fresh = dc_free * md_internal_env_process(&state->amplitude_env) * state->velocity;
        const float output = md_clip(md_retrigger_fade_process(&state->retrigger_fade,
                                                               fresh),
                                     1.0f);
        mono_out[i] = output;
        state->last_output = output;
    }

    if (state->amplitude_env.value == 0.0f)
    {
        instance->triggered = 0U;
        state->last_output = 0.0f;
    }
}

static float md_phase_sine_offset_next(md_phase_t *oscillator, float phase_offset_radians)
{
    md_phase_t evaluation = *oscillator;
    while (phase_offset_radians >= 3.14159265f)
    {
        phase_offset_radians -= 6.28318531f;
    }
    while (phase_offset_radians < -3.14159265f)
    {
        phase_offset_radians += 6.28318531f;
    }
    const int32_t phase_offset = (int32_t)(phase_offset_radians * 683565248.0f);
    evaluation.phase += (uint32_t)phase_offset;
    const float output = md_phase_sine_next(&evaluation);
    oscillator->phase += oscillator->increment;
    return output;
}

static uint32_t md_efm_scaled_increment(uint32_t target_increment, float ratio)
{
    const uint32_t whole = (uint32_t)ratio;
    const float fraction = ratio - (float)whole;
    return (target_increment * whole) + (uint32_t)((float)target_increment * fraction);
}

static float md_efm_pm_next(md_efm_pm_state_t *pm,
                            uint32_t carrier_increment,
                            uint32_t modulator_increment,
                            float modulation_index,
                            float feedback)
{
    pm->carrier.increment = carrier_increment;
    pm->modulator.increment = modulator_increment;
    const float modulator = md_phase_sine_offset_next(&pm->modulator,
                                                       feedback * pm->previous_modulator);
    pm->previous_modulator = modulator;
    return md_phase_sine_offset_next(&pm->carrier, modulation_index * modulator);
}

static void md_efm_bd_prepare(drum_synth_instance_t *instance)
{
    md_efm_bd_state_t *const state = &instance->engine.efm_bd;
    const float decay_u = (float)instance->md_slots[1] * (1.0f / 127.0f);
    const float ramp_u = (float)instance->md_slots[2] * (1.0f / 127.0f);
    const float ramp_decay_u = (float)instance->md_slots[3] * (1.0f / 127.0f);
    const float modulation_u = (float)instance->md_slots[4] * (1.0f / 127.0f);
    const float modulator_frequency_u = (float)instance->md_slots[5] * (1.0f / 127.0f);
    const float modulation_decay_u = (float)instance->md_slots[6] * (1.0f / 127.0f);
    const float feedback_u = (float)instance->md_slots[7] * (1.0f / 127.0f);

    md_internal_env_prepare(&state->amplitude_env, md_expmap(0.025f, 7.3219281f, decay_u));
    md_internal_env_prepare(&state->pitch_env, md_expmap(0.002f, 8.6438562f, ramp_decay_u));
    md_internal_env_prepare(&state->modulation_env,
                       md_expmap(0.002f, 9.9657843f, modulation_decay_u));
    state->ramp_ratio = md_fast_exp2(6.0f * ramp_u);
    state->modulation_index = 12.0f * modulation_u * modulation_u;
    state->modulator_ratio = 0.25f * md_fast_exp2(4.0f * modulator_frequency_u);
    state->feedback = 0.95f * feedback_u * feedback_u;
}

static void md_efm_bd_reset(drum_synth_instance_t *instance)
{
    md_efm_bd_state_t *const state = &instance->engine.efm_bd;
    std::memset(state, 0, sizeof(*state));
    md_efm_bd_prepare(instance);
}

static void md_efm_bd_note_on(drum_synth_instance_t *instance, uint8_t midi_note, uint8_t velocity)
{
    md_efm_bd_state_t *const state = &instance->engine.efm_bd;
    const float pitch_u = (float)instance->md_slots[0] * (1.0f / 127.0f);
    const float base_hz = md_expmap(20.0f, 3.9068906f, pitch_u);
    const float note_ratio = md_fast_exp2(((float)midi_note - 36.0f) * (1.0f / 12.0f));
    const float target_hz = clampf_local(base_hz * note_ratio, 10.0f, 6000.0f);

    md_retrigger_fade_begin(&state->retrigger_fade, state->last_output, 32U);
    md_phase_reset(&state->pm.carrier, 0U);
    md_phase_reset(&state->pm.modulator, 0U);
    state->pm.target_carrier_increment = md_phase_increment_from_hz(target_hz, kMdSampleRate);
    state->pm.target_modulator_increment = md_phase_increment_from_hz(target_hz
                                                                      * state->modulator_ratio,
                                                                      kMdSampleRate);
    state->velocity = clampf_local((float)velocity * (1.0f / 127.0f), 0.0f, 1.0f);
    state->pm.previous_modulator = 0.0f;
    md_internal_env_trigger(&state->amplitude_env, 1.0f);
    md_internal_env_trigger(&state->pitch_env, 1.0f);
    md_internal_env_trigger(&state->modulation_env, 1.0f);
    instance->triggered = 1U;
}

static void md_efm_bd_render(drum_synth_instance_t *instance, float *mono_out, uint32_t frames)
{
    md_efm_bd_state_t *const state = &instance->engine.efm_bd;
    const float pitch_scale = state->ramp_ratio - 1.0f;

    for (uint32_t i = 0U; i < frames; ++i)
    {
        const float pitch_ratio = 1.0f + (pitch_scale * md_internal_env_process(&state->pitch_env));
        const float index = state->modulation_index
            * md_internal_env_process(&state->modulation_env);
        const float carrier = md_efm_pm_next(
            &state->pm,
            md_efm_scaled_increment(state->pm.target_carrier_increment, pitch_ratio),
            md_efm_scaled_increment(state->pm.target_modulator_increment, pitch_ratio),
            index,
            state->feedback);
        const float fresh = carrier * md_internal_env_process(&state->amplitude_env) * state->velocity;
        const float output = md_clip(md_retrigger_fade_process(&state->retrigger_fade,
                                                               fresh),
                                     1.0f);
        mono_out[i] = output;
        state->last_output = output;
    }

    if (state->amplitude_env.value == 0.0f)
    {
        instance->triggered = 0U;
        state->pm.previous_modulator = 0.0f;
        state->last_output = 0.0f;
    }
}

static void md_efm_sd_prepare(drum_synth_instance_t *instance)
{
    md_efm_sd_state_t *const state = &instance->engine.efm_sd;
    const float decay_u = (float)instance->md_slots[1] * (1.0f / 127.0f);
    const float noise_u = (float)instance->md_slots[2] * (1.0f / 127.0f);
    const float noise_decay_u = (float)instance->md_slots[3] * (1.0f / 127.0f);
    const float modulation_u = (float)instance->md_slots[4] * (1.0f / 127.0f);
    const float modulator_frequency_u = (float)instance->md_slots[5] * (1.0f / 127.0f);
    const float modulation_decay_u = (float)instance->md_slots[6] * (1.0f / 127.0f);
    const float highpass_u = (float)instance->md_slots[7] * (1.0f / 127.0f);
    const float highpass_hz = md_expmap(80.0f, 7.4512111f, highpass_u);

    md_internal_env_prepare(&state->amplitude_env, md_expmap(0.020f, 6.9657843f, decay_u));
    md_internal_env_prepare(&state->noise_env, md_expmap(0.003f, 8.9657843f, noise_decay_u));
    md_internal_env_prepare(&state->modulation_env,
                       md_expmap(0.002f, 9.5507468f, modulation_decay_u));
    md_hpf_prepare(&state->highpass, md_filter_pole_from_hz(highpass_hz));
    state->noise_gain = noise_u * noise_u;
    state->modulation_index = 12.0f * modulation_u * modulation_u;
    state->modulator_ratio = 0.25f * md_fast_exp2(5.0f * modulator_frequency_u);
}

static void md_efm_sd_reset(drum_synth_instance_t *instance)
{
    md_efm_sd_state_t *const state = &instance->engine.efm_sd;
    std::memset(state, 0, sizeof(*state));
    md_rng_seed(&state->rng, 0x45464D53UL ^ (uint32_t)(instance - g_drum_instances));
    md_efm_sd_prepare(instance);
}

static void md_efm_sd_note_on(drum_synth_instance_t *instance, uint8_t midi_note, uint8_t velocity)
{
    md_efm_sd_state_t *const state = &instance->engine.efm_sd;
    const float pitch_u = (float)instance->md_slots[0] * (1.0f / 127.0f);
    const float base_hz = md_expmap(70.0f, 2.8365013f, pitch_u);
    const float note_ratio = md_fast_exp2(((float)midi_note - 36.0f) * (1.0f / 12.0f));
    const float target_hz = clampf_local(base_hz * note_ratio, 20.0f, 6000.0f);

    md_retrigger_fade_begin(&state->retrigger_fade, state->last_output, 32U);
    md_phase_reset(&state->pm.carrier, 0U);
    md_phase_reset(&state->pm.modulator, 0U);
    state->pm.target_carrier_increment = md_phase_increment_from_hz(target_hz, kMdSampleRate);
    state->pm.target_modulator_increment = md_phase_increment_from_hz(target_hz
                                                                      * state->modulator_ratio,
                                                                      kMdSampleRate);
    state->pm.previous_modulator = 0.0f;
    state->velocity = clampf_local((float)velocity * (1.0f / 127.0f), 0.0f, 1.0f);
    md_rng_seed(&state->rng, 0x45464D53UL ^ (uint32_t)(instance - g_drum_instances));
    md_hpf_reset(&state->highpass);
    md_internal_env_trigger(&state->amplitude_env, 1.0f);
    md_internal_env_trigger(&state->modulation_env, 1.0f);
    md_internal_env_trigger(&state->noise_env, state->noise_gain);
    instance->triggered = 1U;
}

static void md_efm_sd_render(drum_synth_instance_t *instance, float *mono_out, uint32_t frames)
{
    md_efm_sd_state_t *const state = &instance->engine.efm_sd;

    for (uint32_t i = 0U; i < frames; ++i)
    {
        const float index = state->modulation_index
            * md_internal_env_process(&state->modulation_env);
        const float carrier = md_efm_pm_next(&state->pm,
                                             state->pm.target_carrier_increment,
                                             state->pm.target_modulator_increment,
                                             index,
                                             0.0f);
        const float tonal = carrier * md_internal_env_process(&state->amplitude_env);
        const float noise = md_rng_next_bipolar(&state->rng)
            * md_internal_env_process(&state->noise_env);
        const float filtered = md_hpf_process(&state->highpass, tonal + noise);
        const float fresh = filtered * state->velocity;
        const float output = md_clip(md_retrigger_fade_process(&state->retrigger_fade,
                                                               fresh),
                                     1.0f);
        mono_out[i] = output;
        state->last_output = output;
    }

    if ((state->amplitude_env.value == 0.0f) && (state->noise_env.value == 0.0f))
    {
        instance->triggered = 0U;
        state->pm.previous_modulator = 0.0f;
        state->last_output = 0.0f;
    }
}

static void md_efm_cb_prepare(drum_synth_instance_t *instance)
{
    md_efm_cb_state_t *const state = &instance->engine.efm_cb;
    const float decay_u = (float)instance->md_slots[1] * (1.0f / 127.0f);
    const float snap_u = (float)instance->md_slots[2] * (1.0f / 127.0f);
    const float feedback_u = (float)instance->md_slots[3] * (1.0f / 127.0f);
    const float modulation_u = (float)instance->md_slots[4] * (1.0f / 127.0f);
    const float modulator_frequency_u = (float)instance->md_slots[5] * (1.0f / 127.0f);
    const float modulation_decay_u = (float)instance->md_slots[6] * (1.0f / 127.0f);

    md_internal_env_prepare(&state->amplitude_env, md_expmap(0.020f, 7.2288187f, decay_u));
    md_internal_env_prepare(&state->snap_env, 0.012f);
    md_internal_env_prepare(&state->modulation_env,
                       md_expmap(0.002f, 8.9657843f, modulation_decay_u));
    state->snap_mix = snap_u * snap_u;
    state->feedback = 0.90f * feedback_u * feedback_u;
    state->modulation_index = 10.0f * modulation_u * modulation_u;
    state->modulator_ratio = 0.5f * md_fast_exp2(3.0f * modulator_frequency_u);
}

static void md_efm_cb_reset(drum_synth_instance_t *instance)
{
    md_efm_cb_state_t *const state = &instance->engine.efm_cb;
    std::memset(state, 0, sizeof(*state));
    md_efm_cb_prepare(instance);
}

static void md_efm_cb_note_on(drum_synth_instance_t *instance, uint8_t midi_note, uint8_t velocity)
{
    md_efm_cb_state_t *const state = &instance->engine.efm_cb;
    const float pitch_u = (float)instance->md_slots[0] * (1.0f / 127.0f);
    const float base_hz = md_expmap(100.0f, 3.9068906f, pitch_u);
    const float note_ratio = md_fast_exp2(((float)midi_note - 36.0f) * (1.0f / 12.0f));
    const float branch_a_hz = clampf_local(base_hz * note_ratio, 30.0f, 10000.0f);
    const float branch_b_hz = branch_a_hz * 1.48f;

    md_retrigger_fade_begin(&state->retrigger_fade, state->last_output, 32U);
    md_phase_reset(&state->branch_a.carrier, 0U);
    md_phase_reset(&state->branch_a.modulator, 0U);
    md_phase_reset(&state->branch_b.carrier, 0U);
    md_phase_reset(&state->branch_b.modulator, 0U);
    state->branch_a.target_carrier_increment = md_phase_increment_from_hz(branch_a_hz,
                                                                          kMdSampleRate);
    state->branch_a.target_modulator_increment = md_phase_increment_from_hz(
        branch_a_hz * state->modulator_ratio, kMdSampleRate);
    state->branch_b.target_carrier_increment = md_phase_increment_from_hz(branch_b_hz,
                                                                          kMdSampleRate);
    state->branch_b.target_modulator_increment = md_phase_increment_from_hz(
        branch_b_hz * state->modulator_ratio, kMdSampleRate);
    state->branch_a.previous_modulator = 0.0f;
    state->branch_b.previous_modulator = 0.0f;
    state->velocity = clampf_local((float)velocity * (1.0f / 127.0f), 0.0f, 1.0f);
    md_internal_env_trigger(&state->amplitude_env, 1.0f);
    md_internal_env_trigger(&state->snap_env, 1.0f);
    md_internal_env_trigger(&state->modulation_env, 1.0f);
    instance->triggered = 1U;
}

static void md_efm_cb_render(drum_synth_instance_t *instance, float *mono_out, uint32_t frames)
{
    md_efm_cb_state_t *const state = &instance->engine.efm_cb;

    for (uint32_t i = 0U; i < frames; ++i)
    {
        const float index = state->modulation_index
            * md_internal_env_process(&state->modulation_env);
        const float branch_a = md_efm_pm_next(&state->branch_a,
                                              state->branch_a.target_carrier_increment,
                                              state->branch_a.target_modulator_increment,
                                              index,
                                              state->feedback);
        const float branch_b = md_efm_pm_next(&state->branch_b,
                                              state->branch_b.target_carrier_increment,
                                              state->branch_b.target_modulator_increment,
                                              index,
                                              state->feedback);
        const float metallic = (0.58f * branch_a) + (0.42f * branch_b);
        const float slow = md_internal_env_process(&state->amplitude_env);
        const float fast = md_internal_env_process(&state->snap_env);
        const float amplitude = (slow * (1.0f - (0.75f * state->snap_mix)))
            + (fast * 0.75f * state->snap_mix);
        const float fresh = metallic * amplitude * state->velocity;
        const float output = md_clip(md_retrigger_fade_process(&state->retrigger_fade,
                                                               fresh),
                                     1.0f);
        mono_out[i] = output;
        state->last_output = output;
    }

    if ((state->amplitude_env.value == 0.0f) && (state->snap_env.value == 0.0f))
    {
        instance->triggered = 0U;
        state->branch_a.previous_modulator = 0.0f;
        state->branch_b.previous_modulator = 0.0f;
        state->last_output = 0.0f;
    }
}

static void md_model_reset(drum_synth_instance_t *instance)
{
    switch ((md_model_t)instance->md_model)
    {
        case MD_MODEL_TRX_BD:
            md_trx_bd_reset(instance);
            break;
        case MD_MODEL_TRX_SD:
            md_trx_sd_reset(instance);
            break;
        case MD_MODEL_TRX_CH:
            md_trx_ch_reset(instance);
            break;
        case MD_MODEL_EFM_BD:
            md_efm_bd_reset(instance);
            break;
        case MD_MODEL_EFM_SD:
            md_efm_sd_reset(instance);
            break;
        case MD_MODEL_EFM_CB:
            md_efm_cb_reset(instance);
            break;
        default:
            std::memset(&instance->engine.efm_cb, 0, sizeof(instance->engine.efm_cb));
            break;
    }
}

static void drum_instance_reset_params(drum_synth_instance_t *instance)
{
    instance->midi_note = 36.0f;
    instance->accent = 1.0f;
    instance->pitch = kDefaultPitch;
    instance->frequency_current = plaits::NoteToFrequency(instance->midi_note);
    instance->decay = kDefaultDecay;
    instance->tone = kDefaultTone;
    instance->fm = kDefaultFm;
    instance->triggered = 0U;
    instance->trigger_pending = 0U;
    instance->md_model = (uint8_t)MD_MODEL_TRX_BD;
    const md_model_profile_t *const profile = md_model_profile_get(instance->md_model);
    for (uint8_t slot = 0U; slot < 8U; ++slot)
    {
        instance->md_slots[slot] = profile->defaults[slot];
    }
}

static void drum_instance_init(drum_synth_instance_t *instance)
{
    instance->model = DRUM_MODEL_ID_NONE;
    drum_instance_reset_params(instance);
    instance->initialized = 1U;
}

static void drum_instance_ensure_init(drum_synth_instance_t *instance)
{
    if (instance->initialized == 0U)
    {
        drum_instance_init(instance);
    }
}
}

void drum_synth_init(float sample_rate)
{
    (void)sample_rate;

    for (uint8_t i = 0U; i < BRICK_ENTITY_TOP_LEVEL_COUNT; ++i)
    {
        drum_instance_init(&g_drum_instances[i]);
    }
}

uint8_t drum_synth_set_model_for_instance(uint8_t instance_id, drum_model_id_t model_type)
{
    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        return 0U;
    }

    drum_instance_ensure_init(instance);

    if ((model_type != DRUM_MODEL_ID_NONE)
            && (model_type != DRUM_MODEL_ID_MD)
            && (model_type != DRUM_MODEL_ID_BD_ANALOG))
    {
        return 0U;
    }

    if (instance->model != model_type)
    {
        if (instance->model == DRUM_MODEL_ID_BD_ANALOG)
        {
            instance->engine.bd.~AnalogBassDrum();
        }
        instance->model = model_type;
        instance->triggered = 0U;
        instance->trigger_pending = 0U;
        if (model_type == DRUM_MODEL_ID_MD)
        {
            md_model_reset(instance);
        }
        else if (model_type == DRUM_MODEL_ID_BD_ANALOG)
        {
            new (&instance->engine.bd) plaits::AnalogBassDrum();
            instance->engine.bd.Init();
        }
    }

    return 1U;
}

drum_model_id_t drum_synth_get_model_for_instance(uint8_t instance_id)
{
    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        return DRUM_MODEL_ID_NONE;
    }

    drum_instance_ensure_init(instance);
    return instance->model;
}

void drum_synth_note_on_for_instance(uint8_t instance_id, uint8_t midi_note, uint8_t velocity)
{
    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        return;
    }

    drum_instance_ensure_init(instance);
    if (instance->model == DRUM_MODEL_ID_MD)
    {
        if (instance->md_model == (uint8_t)MD_MODEL_TRX_BD)
        {
            instance->midi_note = (float)midi_note;
            md_trx_bd_note_on(instance, midi_note, velocity);
        }
        else if (instance->md_model == (uint8_t)MD_MODEL_TRX_SD)
        {
            instance->midi_note = (float)midi_note;
            md_trx_sd_note_on(instance, midi_note, velocity);
        }
        else if (instance->md_model == (uint8_t)MD_MODEL_TRX_CH)
        {
            instance->midi_note = (float)midi_note;
            md_trx_ch_note_on(instance, midi_note, velocity);
        }
        else if (instance->md_model == (uint8_t)MD_MODEL_EFM_BD)
        {
            instance->midi_note = (float)midi_note;
            md_efm_bd_note_on(instance, midi_note, velocity);
        }
        else if (instance->md_model == (uint8_t)MD_MODEL_EFM_SD)
        {
            instance->midi_note = (float)midi_note;
            md_efm_sd_note_on(instance, midi_note, velocity);
        }
        else if (instance->md_model == (uint8_t)MD_MODEL_EFM_CB)
        {
            instance->midi_note = (float)midi_note;
            md_efm_cb_note_on(instance, midi_note, velocity);
        }
        return;
    }
    if (instance->model != DRUM_MODEL_ID_BD_ANALOG)
    {
        return;
    }

    instance->midi_note = (float)midi_note;
    instance->frequency_current =
        plaits::NoteToFrequency(instance->midi_note + instance->pitch);
    instance->accent = clampf_local((float)velocity / 127.0f, 0.0f, 1.0f);
    instance->triggered = 1U;
    instance->trigger_pending = 1U;
}

void drum_synth_note_off_for_instance(uint8_t instance_id, uint8_t midi_note)
{
    (void)instance_id;
    (void)midi_note;
}

void drum_synth_all_notes_off_for_instance(uint8_t instance_id)
{
    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        return;
    }

    drum_instance_ensure_init(instance);
    if (instance->model == DRUM_MODEL_ID_MD)
    {
        md_model_reset(instance);
    }
    else if (instance->model == DRUM_MODEL_ID_BD_ANALOG)
    {
        instance->engine.bd.Init();
    }
    instance->triggered = 0U;
    instance->trigger_pending = 0U;
}

void drum_synth_process_block_for_instance(uint8_t instance_id, float *mono_out, uint32_t frames)
{
    if (mono_out == nullptr)
    {
        return;
    }

    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        std::memset(mono_out, 0, (size_t)frames * sizeof(float));
        return;
    }

    drum_instance_ensure_init(instance);
    if (instance->model == DRUM_MODEL_ID_MD)
    {
        if ((instance->md_model == (uint8_t)MD_MODEL_TRX_BD) && (instance->triggered != 0U))
        {
            md_trx_bd_render(instance, mono_out, frames);
        }
        else if ((instance->md_model == (uint8_t)MD_MODEL_TRX_SD)
                && (instance->triggered != 0U))
        {
            md_trx_sd_render(instance, mono_out, frames);
        }
        else if ((instance->md_model == (uint8_t)MD_MODEL_TRX_CH)
                && (instance->triggered != 0U))
        {
            md_trx_ch_render(instance, mono_out, frames);
        }
        else if ((instance->md_model == (uint8_t)MD_MODEL_EFM_BD)
                && (instance->triggered != 0U))
        {
            md_efm_bd_render(instance, mono_out, frames);
        }
        else if ((instance->md_model == (uint8_t)MD_MODEL_EFM_SD)
                && (instance->triggered != 0U))
        {
            md_efm_sd_render(instance, mono_out, frames);
        }
        else if ((instance->md_model == (uint8_t)MD_MODEL_EFM_CB)
                && (instance->triggered != 0U))
        {
            md_efm_cb_render(instance, mono_out, frames);
        }
        else
        {
            std::memset(mono_out, 0, (size_t)frames * sizeof(float));
        }
        return;
    }
    if ((instance->model != DRUM_MODEL_ID_BD_ANALOG) || (instance->triggered == 0U))
    {
        std::memset(mono_out, 0, (size_t)frames * sizeof(float));
        return;
    }

    const bool trigger = (instance->trigger_pending != 0U);
    instance->trigger_pending = 0U;

    const float note = instance->midi_note + instance->pitch;
    const float frequency_target = plaits::NoteToFrequency(note);
    const float accent = clampf_local(instance->accent, 0.0f, 1.0f);
    const float tone = clampf_local(instance->tone, 0.0f, 1.0f);
    const float decay = clampf_local(instance->decay * 0.5f, 0.0f, 1.0f);
    const float attack_fm = clampf_local(instance->fm, 0.0f, 1.0f);

    const float frequency_start = instance->frequency_current;
    for (uint32_t offset = 0U; offset < frames; offset += 8U)
    {
        uint32_t chunk = frames - offset;
        if (chunk > 8U)
        {
            chunk = 8U;
        }
        const float progress = (float)(offset + chunk) / (float)frames;
        const float f0 = frequency_start
            + ((frequency_target - frequency_start) * progress);
        instance->engine.bd.Render(false,
                            trigger && (offset == 0U),
                            accent,
                            f0,
                            tone,
                            decay,
                            attack_fm,
                            0.0f,
                            &mono_out[offset],
                            (size_t)chunk);
    }
    instance->frequency_current = frequency_target;
}

uint8_t drum_synth_set_param_for_instance(uint8_t instance_id, param_id_t param, float value)
{
    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr)
    {
        return 0U;
    }

    drum_instance_ensure_init(instance);
    if ((instance->model == DRUM_MODEL_ID_MD)
            && (param >= PARAM_DRUM_MD_MODEL)
            && (param <= PARAM_DRUM_MD_P8))
    {
        if (param == PARAM_DRUM_MD_MODEL)
        {
            const uint8_t model = md_model_validate(value);
            if (instance->md_model != model)
            {
                instance->md_model = model;
                md_model_reset(instance);
                instance->triggered = 0U;
            }
        }
        else
        {
            instance->md_slots[(uint8_t)(param - PARAM_DRUM_MD_P1)] =
                (uint8_t)(clampf_local(value, 0.0f, 127.0f) + 0.5f);
            if (instance->md_model == (uint8_t)MD_MODEL_TRX_BD)
            {
                md_trx_bd_prepare(instance);
            }
            else if (instance->md_model == (uint8_t)MD_MODEL_TRX_SD)
            {
                md_trx_sd_prepare(instance);
            }
            else if (instance->md_model == (uint8_t)MD_MODEL_TRX_CH)
            {
                md_trx_ch_prepare(instance);
            }
            else if (instance->md_model == (uint8_t)MD_MODEL_EFM_BD)
            {
                md_efm_bd_prepare(instance);
            }
            else if (instance->md_model == (uint8_t)MD_MODEL_EFM_SD)
            {
                md_efm_sd_prepare(instance);
            }
            else if (instance->md_model == (uint8_t)MD_MODEL_EFM_CB)
            {
                md_efm_cb_prepare(instance);
            }
        }
        return 1U;
    }
    if (instance->model != DRUM_MODEL_ID_BD_ANALOG)
    {
        return 0U;
    }

    switch (param)
    {
        case PARAM_DRUM_TRX_BD_PITCH:
            instance->pitch = clampf_local(value, -48.0f, 24.0f);
            return 1U;
        case PARAM_DRUM_TRX_BD_DECAY:
            instance->decay = clampf_local(value, 0.01f, 2.0f);
            return 1U;
        case PARAM_DRUM_TRX_BD_HARMONICS:
            instance->tone = clampf_local(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
            instance->fm = clampf_local(value, 0.0f, 1.0f);
            return 1U;
        default:
            return 0U;
    }
}

uint8_t drum_synth_get_md_model_for_instance(uint8_t instance_id)
{
    drum_synth_instance_t *const instance = drum_instance(instance_id);
    if (instance == nullptr) return (uint8_t)MD_MODEL_TRX_BD;
    drum_instance_ensure_init(instance);
    return instance->md_model;
}

void drum_synth_all_notes_off_all(void)
{
    for (uint8_t i = 0U; i < BRICK_ENTITY_TOP_LEVEL_COUNT; ++i)
    {
        drum_synth_all_notes_off_for_instance(i);
    }
}
