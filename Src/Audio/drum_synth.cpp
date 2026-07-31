#include "Audio/drum_synth.h"
#include "Audio/md_model.h"
#include "Audio/md_dsp.h"

#include <cstring>
#include <new>

#include "Seq/seq_types.h"

#include "plaits/dsp/drums/analog_bass_drum.h"
#include "plaits/dsp/engine/engine.h"

namespace
{
constexpr float kDefaultPitch = 0.0f;
constexpr float kDefaultDecay = 0.4f;
constexpr float kDefaultTone = 0.0f;
constexpr float kDefaultFm = 0.3f;
constexpr float kMdSampleRate = 48000.0f;

typedef struct
{
    float value;
    float coefficient;
} md_trx_env_t;

typedef struct
{
    md_phase_t fundamental;
    md_phase_t harmonic;
    md_trx_env_t amplitude_env;
    md_trx_env_t pitch_env;
    md_trx_env_t transient_env;
    md_trx_env_t noise_env;
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

union drum_engine_state_t
{
    plaits::AnalogBassDrum bd;
    md_trx_bd_state_t trx_bd;
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

static drum_synth_instance_t g_drum_instances[SEQ_TRACK_COUNT];

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
    return (instance_id < SEQ_TRACK_COUNT) ? &g_drum_instances[instance_id] : nullptr;
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

static void md_trx_env_prepare(md_trx_env_t *env, float seconds)
{
    env->coefficient = md_decay_coefficient(seconds, kMdSampleRate);
}

static void md_trx_env_trigger(md_trx_env_t *env, float level)
{
    env->value = clampf_local(level, 0.0f, 1.0f);
}

static float md_trx_env_process(md_trx_env_t *env)
{
    const float output = env->value;
    env->value *= env->coefficient;
    if (env->value <= 1.0e-5f)
    {
        env->value = 0.0f;
    }
    return output;
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

    md_trx_env_prepare(&state->amplitude_env, md_expmap(0.03f, 7.0588937f, decay_u));
    md_trx_env_prepare(&state->pitch_env, md_expmap(0.002f, 8.6438562f, ramp_decay_u));
    md_trx_env_prepare(&state->transient_env, 0.0015f + (0.006f * start_u));
    md_trx_env_prepare(&state->noise_env, 0.003f + (0.025f * noise_u));
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
    md_trx_env_trigger(&state->amplitude_env, 1.0f);
    md_trx_env_trigger(&state->pitch_env, 1.0f);
    md_trx_env_trigger(&state->transient_env, state->transient_gain);
    md_trx_env_trigger(&state->noise_env, state->noise_gain);
    instance->triggered = 1U;
}

static void md_trx_bd_render(drum_synth_instance_t *instance, float *mono_out, uint32_t frames)
{
    md_trx_bd_state_t *const state = &instance->engine.trx_bd;
    const uint32_t target_increment = state->fundamental.increment;
    const float pitch_delta = (float)target_increment * (state->ramp_ratio - 1.0f);

    for (uint32_t i = 0U; i < frames; ++i)
    {
        const float pitch_env = md_trx_env_process(&state->pitch_env);
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
        const float transient = md_trx_env_process(&state->transient_env);
        const float noise = md_rng_next_bipolar(&state->rng) * md_trx_env_process(&state->noise_env);
        const float amplitude = md_trx_env_process(&state->amplitude_env);
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

    for (uint8_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
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
            md_trx_bd_reset(instance);
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
        md_trx_bd_reset(instance);
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
                const md_model_profile_t *const profile = md_model_profile_get(instance->md_model);
                for (uint8_t slot = 0U; slot < 8U; ++slot)
                {
                    instance->md_slots[slot] = profile->defaults[slot];
                }
                md_trx_bd_reset(instance);
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

void drum_synth_all_notes_off_all(void)
{
    for (uint8_t i = 0U; i < SEQ_TRACK_COUNT; ++i)
    {
        drum_synth_all_notes_off_for_instance(i);
    }
}
