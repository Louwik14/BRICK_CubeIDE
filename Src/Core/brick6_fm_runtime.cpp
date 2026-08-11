#include "Core/brick6_fm_runtime.h"

#include <math.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "EngineMkI.h"
#include "EngineOpl.h"
#include "msfa/env.h"
#include "msfa/exp2.h"
#include "msfa/fm_core.h"
#include "msfa/fm_op_kernel.h"
#include "msfa/freqlut.h"
#include "msfa/pitchenv.h"
#include "msfa/sin.h"

namespace
{
constexpr uint32_t kSampleRate = 48000U;
constexpr int32_t kQ24 = 1 << 24;
constexpr int kOperatorCount = 6;
constexpr int kDefaultAlgorithm = 0;
constexpr uint8_t kDefaultFeedback = 0U;
constexpr uint8_t kDefaultSync = 1U;

struct fm_voice_t
{
    Env env[kOperatorCount];
    PitchEnv pitch_env;
    FmOpParams operators[kOperatorCount];
    int32_t feedback[2];
    int32_t base_log_frequency[kOperatorCount];
    int32_t operator_level_offset[kOperatorCount];
    uint8_t mode;
    uint8_t algorithm;
    uint8_t feedback_amount;
    uint8_t sync;
    float bright;
    float body;
    float detail;
    float metal;
    float env_attack;
    float env_decay;
    float env_sustain;
    float env_release;
    uint8_t note;
    uint8_t velocity;
    uint8_t active;
};

AUDIO_HOT static fm_voice_t g_fm_voice[BRICK6_FM_VOICE_COUNT];
AUDIO_HOT static FmCore g_fm_modern;
AUDIO_HOT static EngineMkI g_fm_mark_i;
AUDIO_HOT static EngineOpl g_fm_opl;

static const uint8_t kOperatorRatios[kOperatorCount] = { 1U, 2U, 3U, 4U, 5U, 6U };
static const uint8_t kOperatorLevels[kOperatorCount] = { 99U, 82U, 76U, 70U, 64U, 58U };
static const int kEnvelopeRates[4] = { 99, 92, 80, 72 };
static const int kEnvelopeLevels[4] = { 99, 92, 80, 0 };
static const int kPitchEnvelopeRates[4] = { 0, 0, 0, 0 };
static const int kPitchEnvelopeLevels[4] = { 49, 49, 49, 49 };
static const uint8_t kAlgorithmFlags[32][kOperatorCount] = {
    {0xc1,0x11,0x11,0x14,0x01,0x14}, {0x01,0x11,0x11,0x14,0xc1,0x14},
    {0xc1,0x11,0x14,0x01,0x11,0x14}, {0xc1,0x11,0x94,0x01,0x11,0x14},
    {0xc1,0x14,0x01,0x14,0x01,0x14}, {0xc1,0x94,0x01,0x14,0x01,0x14},
    {0xc1,0x11,0x05,0x14,0x01,0x14}, {0x01,0x11,0xc5,0x14,0x01,0x14},
    {0x01,0x11,0x05,0x14,0xc1,0x14}, {0x01,0x05,0x14,0xc1,0x11,0x14},
    {0xc1,0x05,0x14,0x01,0x11,0x14}, {0x01,0x05,0x05,0x14,0xc1,0x14},
    {0xc1,0x05,0x05,0x14,0x01,0x14}, {0xc1,0x05,0x11,0x14,0x01,0x14},
    {0x01,0x05,0x11,0x14,0xc1,0x14}, {0xc1,0x11,0x02,0x25,0x05,0x14},
    {0x01,0x11,0x02,0x25,0xc5,0x14}, {0x01,0x11,0x11,0xc5,0x05,0x14},
    {0xc1,0x14,0x14,0x01,0x11,0x14}, {0x01,0x05,0x14,0xc1,0x14,0x14},
    {0x01,0x14,0x14,0xc1,0x14,0x14}, {0xc1,0x14,0x14,0x14,0x01,0x14},
    {0xc1,0x14,0x14,0x01,0x14,0x04}, {0xc1,0x14,0x14,0x14,0x04,0x04},
    {0xc1,0x14,0x14,0x04,0x04,0x04}, {0xc1,0x05,0x14,0x01,0x14,0x04},
    {0x01,0x05,0x14,0xc1,0x14,0x04}, {0x04,0xc1,0x11,0x14,0x01,0x14},
    {0xc1,0x14,0x01,0x14,0x04,0x04}, {0x04,0xc1,0x11,0x14,0x04,0x04},
    {0xc1,0x14,0x04,0x04,0x04,0x04}, {0xc4,0x04,0x04,0x04,0x04,0x04}
};

static float clamp_macro(float value)
{
    return (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
}

static int macro_delta(float value, int span)
{
    return (int)((clamp_macro(value) - 0.5f) * (float)span);
}

static uint8_t operator_feeds_carrier(uint8_t algorithm, int op)
{
    const uint8_t output_bus = (uint8_t)(kAlgorithmFlags[algorithm & 31U][op] & 0x03U);
    if (output_bus == 0U)
        return 0U;
    for (int candidate = 0; candidate < kOperatorCount; ++candidate)
    {
        const uint8_t flags = kAlgorithmFlags[algorithm & 31U][candidate];
        if (((flags & 0x04U) != 0U) && (((flags >> 4) & 0x03U) == output_bus))
            return 1U;
    }
    return 0U;
}

static uint8_t operator_feeds_modulator(uint8_t algorithm, int op)
{
    const uint8_t output_bus = (uint8_t)(kAlgorithmFlags[algorithm & 31U][op] & 0x03U);
    if (output_bus == 0U)
        return 0U;
    for (int candidate = 0; candidate < kOperatorCount; ++candidate)
    {
        const uint8_t flags = kAlgorithmFlags[algorithm & 31U][candidate];
        if (((flags & 0x04U) == 0U) && (((flags >> 4) & 0x03U) == output_bus))
            return 1U;
    }
    return 0U;
}

static int32_t note_log_frequency(uint8_t note, float ratio);

static float operator_gain_factor(const fm_voice_t *voice, int op)
{
    if ((voice == nullptr) || FmCore::isCarrier(voice->algorithm, op))
        return 1.0f;
    const bool direct_modulator = operator_feeds_carrier(voice->algorithm, op) != 0U;
    const bool deep_modulator = (operator_feeds_modulator(voice->algorithm, op) != 0U)
        && !direct_modulator;
    float factor = 0.35f + (1.30f * voice->bright);
    if (direct_modulator)
        factor *= 1.0f + (voice->body - 0.5f) * 1.4f;
    if (deep_modulator)
        factor *= 1.0f + (voice->detail - 0.5f) * 1.6f;
    return (factor < 0.05f) ? 0.05f : factor;
}

static void refresh_voice_patch(fm_voice_t *voice)
{
    if (voice == nullptr)
        return;
    for (int op = 0; op < kOperatorCount; ++op)
    {
        voice->operator_level_offset[op] =
            (int32_t)(log2f(operator_gain_factor(voice, op)) * (float)kQ24);
        if (voice->active != 0U)
        {
            const bool carrier = FmCore::isCarrier(voice->algorithm, op);
            const float ratio = (carrier != false)
                ? (float)kOperatorRatios[op]
                : ((float)kOperatorRatios[op] * (1.0f + ((voice->metal - 0.5f) * 0.20f)));
            voice->base_log_frequency[op] = note_log_frequency(voice->note, ratio);
        }
    }
}

static uint8_t valid_instance(uint8_t instance_id)
{
    return (instance_id < BRICK6_FM_VOICE_COUNT) ? 1U : 0U;
}

static uint8_t clamp_algorithm(uint8_t algorithm)
{
    return (algorithm < 32U) ? algorithm : 31U;
}

static uint8_t feedback_shift(uint8_t feedback)
{
    if (feedback == 0U)
        return 16U;
    const uint8_t shift = (feedback >= 8U) ? 0U : (uint8_t)(8U - feedback);
    return shift;
}

static int32_t note_log_frequency(uint8_t note, float ratio)
{
    const float hz = 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f)
        * (float)ratio;
    return (int32_t)(log2f(hz) * (float)kQ24);
}

static void reset_voice(fm_voice_t *voice)
{
    if (voice == nullptr)
        return;

    memset(voice->operators, 0, sizeof(voice->operators));
    memset(voice->feedback, 0, sizeof(voice->feedback));
    memset(voice->base_log_frequency, 0, sizeof(voice->base_log_frequency));
    voice->mode = (uint8_t)BRICK6_FM_MODE_MODERN;
    voice->algorithm = (uint8_t)kDefaultAlgorithm;
    voice->feedback_amount = kDefaultFeedback;
    voice->sync = kDefaultSync;
    voice->bright = 0.5f;
    voice->body = 0.5f;
    voice->detail = 0.5f;
    voice->metal = 0.5f;
    voice->env_attack = 0.5f;
    voice->env_decay = 0.5f;
    voice->env_sustain = 0.5f;
    voice->env_release = 0.5f;
    voice->note = 0U;
    voice->velocity = 0U;
    voice->active = 0U;
    for (int op = 0; op < kOperatorCount; ++op)
    {
        voice->env[op].init(kEnvelopeRates,
                            kEnvelopeLevels,
                            (int)kOperatorLevels[op] << 5,
                            0);
    }
    voice->pitch_env.set(kPitchEnvelopeRates, kPitchEnvelopeLevels);
    refresh_voice_patch(voice);
}

static FmCore *engine_for_mode(uint8_t mode)
{
    switch ((brick6_fm_mode_t)mode)
    {
        case BRICK6_FM_MODE_MARK_I: return &g_fm_mark_i;
        case BRICK6_FM_MODE_OPL: return &g_fm_opl;
        case BRICK6_FM_MODE_MODERN:
        default: return &g_fm_modern;
    }
}

static void prepare_note(fm_voice_t *voice, uint8_t note, uint8_t velocity)
{
    voice->note = note;
    voice->velocity = velocity;
    voice->active = 1U;
    if (voice->sync != 0U)
    {
        memset(voice->feedback, 0, sizeof(voice->feedback));
        for (int op = 0; op < kOperatorCount; ++op)
            voice->operators[op].phase = 0;
    }
    for (int op = 0; op < kOperatorCount; ++op)
    {
        int rates[4] = {
            kEnvelopeRates[0] + macro_delta(voice->env_attack, 18),
            kEnvelopeRates[1] + macro_delta(voice->env_decay, 18),
            kEnvelopeRates[2],
            kEnvelopeRates[3] + macro_delta(voice->env_release, 18)
        };
        int levels[4] = {
            kEnvelopeLevels[0],
            kEnvelopeLevels[1],
            kEnvelopeLevels[2] + macro_delta(voice->env_sustain, 38),
            kEnvelopeLevels[3]
        };
        for (int stage = 0; stage < 4; ++stage)
        {
            if (rates[stage] < 0) rates[stage] = 0;
            if (rates[stage] > 99) rates[stage] = 99;
            if (levels[stage] < 0) levels[stage] = 0;
            if (levels[stage] > 99) levels[stage] = 99;
        }
        const bool carrier = FmCore::isCarrier(voice->algorithm, op);
        const float metal_ratio = (carrier != false)
            ? (float)kOperatorRatios[op]
            : ((float)kOperatorRatios[op] * (1.0f + ((voice->metal - 0.5f) * 0.20f)));
        voice->env[op].init(rates,
                            levels,
                            (int)kOperatorLevels[op] << 5,
                            0);
        voice->base_log_frequency[op] = note_log_frequency(note, metal_ratio);
        voice->operators[op].freq = Freqlut::lookup(voice->base_log_frequency[op]);
        voice->operators[op].gain_out = 0;
        voice->env[op].keydown(true);
    }
    refresh_voice_patch(voice);
    voice->pitch_env.set(kPitchEnvelopeRates, kPitchEnvelopeLevels);
    voice->pitch_env.keydown(true);
}
}

void brick6_fm_runtime_init(void)
{
    Sin::init();
    Exp2::init();
    Freqlut::init((double)kSampleRate);
    Env::init_sr((double)kSampleRate);
    PitchEnv::init((double)kSampleRate);
    for (uint8_t instance = 0U; instance < BRICK6_FM_VOICE_COUNT; ++instance)
        reset_voice(&g_fm_voice[instance]);
}

void brick6_fm_runtime_reset_instance(uint8_t instance_id)
{
    if (valid_instance(instance_id) != 0U)
        reset_voice(&g_fm_voice[instance_id]);
}

void brick6_fm_runtime_all_notes_off(uint8_t instance_id)
{
    if (valid_instance(instance_id) == 0U)
        return;
    fm_voice_t *const voice = &g_fm_voice[instance_id];
    for (int op = 0; op < kOperatorCount; ++op)
        voice->env[op].keydown(false);
    voice->pitch_env.keydown(false);
}

void brick6_fm_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity)
{
    if (valid_instance(instance_id) != 0U)
        prepare_note(&g_fm_voice[instance_id], note, velocity);
}

void brick6_fm_runtime_note_off(uint8_t instance_id, uint8_t note)
{
    if ((valid_instance(instance_id) == 0U) || (g_fm_voice[instance_id].note != note))
        return;
    brick6_fm_runtime_all_notes_off(instance_id);
}

void brick6_fm_runtime_set_mode(uint8_t instance_id, brick6_fm_mode_t mode)
{
    if ((valid_instance(instance_id) != 0U) && (mode < BRICK6_FM_MODE_COUNT))
        g_fm_voice[instance_id].mode = (uint8_t)mode;
}

void brick6_fm_runtime_set_algorithm(uint8_t instance_id, uint8_t algorithm)
{
    if (valid_instance(instance_id) != 0U)
    {
        g_fm_voice[instance_id].algorithm = clamp_algorithm(algorithm);
        refresh_voice_patch(&g_fm_voice[instance_id]);
    }
}

void brick6_fm_runtime_set_feedback(uint8_t instance_id, uint8_t feedback)
{
    if (valid_instance(instance_id) != 0U)
    {
        g_fm_voice[instance_id].feedback_amount = (feedback > 8U) ? 8U : feedback;
    }
}

void brick6_fm_runtime_set_sync(uint8_t instance_id, uint8_t enabled)
{
    if (valid_instance(instance_id) != 0U)
        g_fm_voice[instance_id].sync = (enabled != 0U) ? 1U : 0U;
}

void brick6_fm_runtime_set_bright(uint8_t instance_id, float value)
{
    if (valid_instance(instance_id) != 0U)
    {
        g_fm_voice[instance_id].bright = clamp_macro(value);
        refresh_voice_patch(&g_fm_voice[instance_id]);
    }
}

void brick6_fm_runtime_set_body(uint8_t instance_id, float value)
{
    if (valid_instance(instance_id) != 0U)
    {
        g_fm_voice[instance_id].body = clamp_macro(value);
        refresh_voice_patch(&g_fm_voice[instance_id]);
    }
}

void brick6_fm_runtime_set_detail(uint8_t instance_id, float value)
{
    if (valid_instance(instance_id) != 0U)
    {
        g_fm_voice[instance_id].detail = clamp_macro(value);
        refresh_voice_patch(&g_fm_voice[instance_id]);
    }
}

void brick6_fm_runtime_set_metal(uint8_t instance_id, float value)
{
    if (valid_instance(instance_id) != 0U)
    {
        g_fm_voice[instance_id].metal = clamp_macro(value);
        refresh_voice_patch(&g_fm_voice[instance_id]);
    }
}

void brick6_fm_runtime_set_env(uint8_t instance_id,
                               float attack,
                               float decay,
                               float sustain,
                               float release)
{
    if (valid_instance(instance_id) == 0U)
        return;
    fm_voice_t *const voice = &g_fm_voice[instance_id];
    voice->env_attack = clamp_macro(attack);
    voice->env_decay = clamp_macro(decay);
    voice->env_sustain = clamp_macro(sustain);
    voice->env_release = clamp_macro(release);
}

void brick6_fm_runtime_sync_voice(uint8_t source_instance_id, uint8_t destination_instance_id)
{
    if ((valid_instance(source_instance_id) == 0U)
            || (valid_instance(destination_instance_id) == 0U))
        return;
    const fm_voice_t *const source = &g_fm_voice[source_instance_id];
    fm_voice_t *const destination = &g_fm_voice[destination_instance_id];
    destination->mode = source->mode;
    destination->algorithm = source->algorithm;
    destination->feedback_amount = source->feedback_amount;
    destination->sync = source->sync;
    destination->bright = source->bright;
    destination->body = source->body;
    destination->detail = source->detail;
    destination->metal = source->metal;
    destination->env_attack = source->env_attack;
    destination->env_decay = source->env_decay;
    destination->env_sustain = source->env_sustain;
    destination->env_release = source->env_release;
}

uint8_t brick6_fm_runtime_render_instance(uint8_t instance_id,
                                          float *out_mono,
                                          uint32_t frames)
{
    if ((valid_instance(instance_id) == 0U) || (out_mono == nullptr)
            || (frames == 0U) || (frames > BRICK6_FM_RENDER_BLOCK))
        return 0U;

    fm_voice_t *const voice = &g_fm_voice[instance_id];
    if (voice->active == 0U)
    {
        memset(out_mono, 0, frames * sizeof(float));
        return 0U;
    }

    int32_t block[BRICK6_FM_RENDER_BLOCK] = { 0 };
    const int32_t pitch_log_frequency = voice->pitch_env.getsample();
    for (int op = 0; op < kOperatorCount; ++op)
    {
        voice->operators[op].level_in = voice->env[op].getsample();
        voice->operators[op].level_in += voice->operator_level_offset[op];
        voice->operators[op].freq = Freqlut::lookup(voice->base_log_frequency[op]
                                                     + pitch_log_frequency);
    }
    engine_for_mode(voice->mode)->render(block,
                                         voice->operators,
                                         voice->algorithm,
                                         voice->feedback,
                                         feedback_shift(voice->feedback_amount));

    constexpr float kOutputScale = 0.125f / (float)kQ24;
    for (uint32_t i = 0U; i < frames; ++i)
        out_mono[i] = (float)block[i] * kOutputScale;

    uint8_t carrier_active = 0U;
    for (int op = 0; op < kOperatorCount; ++op)
    {
        if (FmCore::isCarrier(voice->algorithm, op) && voice->env[op].isActive())
        {
            carrier_active = 1U;
            break;
        }
    }
    if (carrier_active == 0U)
    {
        voice->active = 0U;
        memset(out_mono, 0, frames * sizeof(float));
        return 0U;
    }
    return 1U;
}
