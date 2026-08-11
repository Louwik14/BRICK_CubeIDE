#include "Core/brick6_fm_runtime.h"

#include <math.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Core/fm_dexed/msfa/env.h"
#include "Core/fm_dexed/msfa/exp2.h"
#include "Core/fm_dexed/msfa/fm_core.h"
#include "Core/fm_dexed/msfa/fm_op_kernel.h"
#include "Core/fm_dexed/msfa/freqlut.h"
#include "Core/fm_dexed/msfa/pitchenv.h"
#include "Core/fm_dexed/msfa/sin.h"
#include "Core/fm_dx7_log_kernel.h"

#ifndef FM_KERNEL_BENCH
#define FM_KERNEL_BENCH 0
#endif

#if (FM_KERNEL_BENCH != 0) && (FM_KERNEL_BENCH != 1)
#error "FM_KERNEL_BENCH must be 0 or 1"
#endif

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
    uint8_t operator_level[kOperatorCount];
    float operator_frequency[kOperatorCount];
    int8_t operator_detune[kOperatorCount];
    uint8_t operator_env[kOperatorCount][4];
    uint8_t operator_on[kOperatorCount];
    uint8_t operator_mode[kOperatorCount];
    uint8_t operator_velocity[kOperatorCount];
    uint8_t operator_key[kOperatorCount];
    float ratio;
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
    float play_velocity;
    float play_key;
    float pitch_env_amount;
    float pitch_env_time;
    uint8_t note;
    uint8_t velocity;
    uint8_t active;
    uint32_t parameter_generation;
    uint32_t applied_source_generation;
#if FM_KERNEL_BENCH
    dx7_log_kernel_voice_t log_kernel;
#endif
};

AUDIO_HOT static fm_voice_t g_fm_voice[BRICK6_FM_VOICE_COUNT];
AUDIO_HOT static FmCore g_fm_modern;

static const uint8_t kOperatorRatios[kOperatorCount] = { 1U, 2U, 3U, 4U, 5U, 6U };
static const uint8_t kOperatorLevels[kOperatorCount] = { 99U, 82U, 76U, 70U, 64U, 58U };
static const int kEnvelopeRates[4] = { 99, 92, 80, 72 };
static const int kEnvelopeLevels[4] = { 99, 92, 80, 0 };
static const int kPitchEnvelopeRates[4] = { 0, 0, 0, 0 };
static const int kPitchEnvelopeLevels[4] = { 49, 49, 49, 49 };
static const int32_t kCoarseMul[32] = {
    -16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600,
    50331648, 53182516, 55732705, 58039632, 60145690, 62083076, 63876816,
    65546747, 67108864, 68576247, 69959732, 71268397, 72509921, 73690858,
    74816848, 75892776, 76922906, 77910978, 78860292, 79773775, 80654032,
    81503396, 82323963, 83117622
};
static const uint8_t kVelocityData[64] = {
    0,70,86,97,106,114,121,126,132,138,142,148,152,156,160,163,
    166,170,173,174,178,181,184,186,189,190,194,196,198,200,202,205,
    206,209,211,214,216,218,220,222,224,225,227,229,230,232,233,235,
    237,238,240,241,242,243,244,246,246,248,249,250,251,252,253,254
};
static const uint8_t kExpScaleData[33] = {
    0,1,2,3,4,5,6,7,8,9,11,14,16,19,23,27,33,39,47,56,66,80,94,110,
    126,142,158,174,190,206,222,238,250
};
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

constexpr uint8_t brick_operator_to_msfa_index(uint8_t brick_operator)
{
    return (uint8_t)((kOperatorCount - 1) - brick_operator);
}

static_assert(brick_operator_to_msfa_index(0U) == 5U, "BRICK OP1 must target DX7 OP1");
static_assert(brick_operator_to_msfa_index(5U) == 0U, "BRICK OP6 must target DX7 OP6");

static int macro_delta(float value, int span)
{
    return (int)((clamp_macro(value) - 0.5f) * (float)span);
}

static uint8_t operator_is_carrier(uint8_t algorithm, int op)
{
    return (uint8_t)((kAlgorithmFlags[algorithm & 31U][op] & 0x07U) == 0x04U);
}

static void algorithm_edges(uint8_t algorithm, uint8_t edges[kOperatorCount])
{
    uint8_t bus_sources[3] = { 0U, 0U, 0U };
    memset(edges, 0, kOperatorCount);
    for (int op = 0; op < kOperatorCount; ++op)
    {
        const uint8_t flags = kAlgorithmFlags[algorithm & 31U][op];
        const uint8_t input_bus = (uint8_t)((flags >> 4) & 0x03U);
        const uint8_t output_bus = (uint8_t)(flags & 0x03U);
        if ((input_bus != 0U) && (input_bus < 3U))
        {
            const uint8_t sources = bus_sources[input_bus];
            for (int source = 0; source < kOperatorCount; ++source)
                if ((sources & (uint8_t)(1U << source)) != 0U)
                    edges[source] |= (uint8_t)(1U << op);
        }
        if ((output_bus != 0U) && (output_bus < 3U))
        {
            const uint8_t self = (uint8_t)(1U << op);
            bus_sources[output_bus] = ((flags & 0x04U) != 0U)
                ? (uint8_t)(bus_sources[output_bus] | self) : self;
        }
    }
}

static uint8_t operator_reaches_carrier(uint8_t algorithm, int op,
                                        const uint8_t edges[kOperatorCount])
{
    uint8_t pending = edges[op];
    uint8_t visited = 0U;
    while (pending != 0U)
    {
        int candidate = 0;
        while ((pending & (uint8_t)(1U << candidate)) == 0U) ++candidate;
        pending &= (uint8_t)~(1U << candidate);
        if (operator_is_carrier(algorithm, candidate) != 0U) return 1U;
        if ((visited & (uint8_t)(1U << candidate)) == 0U)
        {
            visited |= (uint8_t)(1U << candidate);
            pending |= edges[candidate];
        }
    }
    return 0U;
}

static uint8_t operator_is_direct_modulator(uint8_t algorithm, int op,
                                            const uint8_t edges[kOperatorCount])
{
    for (int candidate = 0; candidate < kOperatorCount; ++candidate)
        if (((edges[op] & (uint8_t)(1U << candidate)) != 0U)
                && (operator_is_carrier(algorithm, candidate) != 0U))
            return 1U;
    return 0U;
}

static float operator_metal_semitones(const fm_voice_t *voice, int op)
{
    if (voice == nullptr || operator_is_carrier(voice->algorithm, op))
        return 0.0f;
    uint8_t edges[kOperatorCount];
    algorithm_edges(voice->algorithm, edges);
    const float amount = (voice->metal - 0.5f) * 2.0f;
    const bool direct_modulator = operator_is_direct_modulator(voice->algorithm, op, edges) != 0U;
    const bool deep_modulator = !direct_modulator
        && (operator_reaches_carrier(voice->algorithm, op, edges) != 0U);
    const float span = direct_modulator ? 7.0f : (deep_modulator ? 13.0f : 9.0f);
    return amount * span;
}

static void ratio_to_dx_frequency(float ratio, int *coarse, int *fine)
{
    float best_error = 1.0e30f;
    int best_coarse = 0;
    int best_fine = 0;
    for (int candidate = 0; candidate < 32; ++candidate)
    {
        const float base = (candidate == 0) ? 0.5f : (float)candidate;
        int candidate_fine = (int)(((ratio / base) - 1.0f) * 100.0f + 0.5f);
        if (candidate_fine < 0) candidate_fine = 0;
        if (candidate_fine > 99) candidate_fine = 99;
        const float represented = base * (1.0f + 0.01f * (float)candidate_fine);
        const float error = fabsf(represented - ratio);
        if (error < best_error)
        {
            best_error = error;
            best_coarse = candidate;
            best_fine = candidate_fine;
        }
    }
    *coarse = best_coarse;
    *fine = best_fine;
}

static void fixed_to_dx_frequency(float brick_frequency, int *coarse, int *fine)
{
    const float hz = 440.0f * brick_frequency;
    int code = (int)(log10f(hz) * 100.0f + 0.5f);
    if (code < 0) code = 0;
    if (code > 399) code = 399;
    *coarse = code / 100;
    *fine = code % 100;
}

static int32_t operator_log_frequency(const fm_voice_t *voice, uint8_t note, int op)
{
    int coarse = 0;
    int fine = 0;
    const int mode = voice->operator_mode[op];
    float frequency = voice->operator_frequency[op];
    if (operator_is_carrier(voice->algorithm, op) == 0U)
    {
        const float bipolar = (voice->ratio - 0.5f) * 2.0f;
        const int step = (int)(fabsf(bipolar) * 3.0f + 0.5f);
        if (step != 0) frequency *= (bipolar < 0.0f) ? (1.0f / (float)(step + 1))
                                                     : (float)(step + 1);
    }
    if (mode == 0) ratio_to_dx_frequency(frequency, &coarse, &fine);
    else fixed_to_dx_frequency(frequency, &coarse, &fine);

    const int detune = (int)voice->operator_detune[op] + 7;
    int32_t log_frequency;
    if (mode == 0)
    {
        log_frequency = 50857777 + (int32_t)note * (kQ24 / 12);
        const double detune_ratio = 0.0209 * exp(-0.396 * ((double)log_frequency / kQ24)) / 7.0;
        log_frequency += (int32_t)(detune_ratio * log_frequency * (detune - 7));
        log_frequency += kCoarseMul[coarse & 31];
        if (fine != 0)
            log_frequency += (int32_t)floor(24204406.323123 * log(1.0 + 0.01 * fine) + 0.5);
    }
    else
    {
        log_frequency = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
        log_frequency += 13457 * (detune - 7);
    }
    return log_frequency
        + (int32_t)((operator_metal_semitones(voice, op) / 12.0f) * (float)kQ24);
}

static float operator_gain_factor(const fm_voice_t *voice, int op)
{
    if (voice == nullptr)
        return 1.0f;
    float factor = 1.0f;
    if (!operator_is_carrier(voice->algorithm, op))
    {
        uint8_t edges[kOperatorCount];
        algorithm_edges(voice->algorithm, edges);
        const bool direct_modulator = operator_is_direct_modulator(voice->algorithm, op, edges) != 0U;
        const bool deep_modulator = !direct_modulator
            && (operator_reaches_carrier(voice->algorithm, op, edges) != 0U);
        factor = 0.35f + (1.30f * voice->bright);
        if (direct_modulator)
            factor *= 1.0f + (voice->body - 0.5f) * 1.4f;
        if (deep_modulator)
            factor *= 1.0f + (voice->detail - 0.5f) * 1.6f;
    }
    return (factor < 0.01f) ? 0.01f : factor;
}

static int scale_velocity(int velocity, int sensitivity)
{
    const int clamped = (velocity < 0) ? 0 : ((velocity > 127) ? 127 : velocity);
    const int value = (int)kVelocityData[clamped >> 1] - 239;
    return ((sensitivity * value + 7) >> 3) << 4;
}

static int scale_curve(int group, int depth, int curve)
{
    int scale;
    if ((curve == 0) || (curve == 3)) scale = (group * depth * 329) >> 12;
    else
    {
        const int index = (group < 32) ? group : 32;
        scale = ((int)kExpScaleData[index] * depth * 329) >> 15;
    }
    return (curve < 2) ? -scale : scale;
}

static int scale_level(int note, int depth)
{
    const int offset = note - 60;
    if (offset >= 0) return scale_curve((offset + 1) / 3, depth, 3);
    return scale_curve(-(offset - 1) / 3, depth, 0);
}

static int operator_output_level(const fm_voice_t *voice, int op)
{
    int level = Env::scaleoutlevel(voice->operator_level[op]);
    level += scale_level(voice->note,
                         (int)((float)voice->operator_key[op] * voice->play_key + 0.5f));
    if (level > 127) level = 127;
    level <<= 5;
    level += scale_velocity(voice->velocity,
                            (int)((float)voice->operator_velocity[op]
                                  * voice->play_velocity + 0.5f));
    return (level < 0) ? 0 : level;
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
            voice->base_log_frequency[op] = operator_log_frequency(voice, voice->note, op);
        }
    }
}

static void refresh_operator_frequency(fm_voice_t *voice, int op)
{
    if ((voice == nullptr) || (op < 0) || (op >= kOperatorCount)
            || (voice->active == 0U))
        return;
    voice->base_log_frequency[op] = operator_log_frequency(voice, voice->note, op);
}

static uint8_t valid_instance(uint8_t instance_id)
{
    return (instance_id < BRICK6_FM_VOICE_COUNT) ? 1U : 0U;
}

static void mark_parameters_changed(fm_voice_t *voice)
{
    if (voice == nullptr)
        return;
    voice->parameter_generation++;
    if (voice->parameter_generation == 0U)
        voice->parameter_generation = 1U;
    voice->applied_source_generation = voice->parameter_generation;
}

static uint8_t clamp_algorithm(uint8_t algorithm)
{
    return (algorithm < 32U) ? algorithm : 31U;
}

static uint8_t feedback_shift(uint8_t feedback)
{
    if (feedback == 0U)
        return 16U;
    return (uint8_t)(8U - ((feedback > 7U) ? 7U : feedback));
}

#if FM_KERNEL_BENCH
static uint8_t log_feedback_shift(uint8_t feedback)
{
    if (feedback == 0U)
        return 17U;
    return (uint8_t)(9U - ((feedback > 7U) ? 7U : feedback));
}
#endif

static void reset_voice(fm_voice_t *voice)
{
    if (voice == nullptr)
        return;

    memset(voice->operators, 0, sizeof(voice->operators));
    memset(voice->feedback, 0, sizeof(voice->feedback));
    memset(voice->base_log_frequency, 0, sizeof(voice->base_log_frequency));
    memset(voice->operator_level, 0, sizeof(voice->operator_level));
    memset(voice->operator_frequency, 0, sizeof(voice->operator_frequency));
    memset(voice->operator_detune, 0, sizeof(voice->operator_detune));
    memset(voice->operator_env, 0, sizeof(voice->operator_env));
    memset(voice->operator_on, 1, sizeof(voice->operator_on));
    memset(voice->operator_mode, 0, sizeof(voice->operator_mode));
    memset(voice->operator_velocity, 7, sizeof(voice->operator_velocity));
    memset(voice->operator_key, 0, sizeof(voice->operator_key));
    voice->ratio = 0.5f;
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
    voice->play_velocity = 1.0f;
    voice->play_key = 0.0f;
    voice->pitch_env_amount = 0.0f;
    voice->pitch_env_time = 0.5f;
    voice->note = 0U;
    voice->velocity = 0U;
    voice->active = 0U;
    voice->parameter_generation = 1U;
    voice->applied_source_generation = 1U;
#if FM_KERNEL_BENCH
    dx7_log_kernel_reset(&voice->log_kernel);
#endif
    for (uint8_t brick_op = 0U; brick_op < kOperatorCount; ++brick_op)
    {
        const uint8_t op = brick_operator_to_msfa_index(brick_op);
        voice->operator_level[op] = (uint8_t)kOperatorLevels[brick_op];
        voice->operator_frequency[op] = (float)kOperatorRatios[brick_op];
        voice->operator_env[op][0] = (uint8_t)kEnvelopeRates[0];
        voice->operator_env[op][1] = (uint8_t)kEnvelopeRates[1];
        voice->operator_env[op][2] = (uint8_t)kEnvelopeLevels[2];
        voice->operator_env[op][3] = (uint8_t)kEnvelopeRates[3];
        voice->env[op].init(kEnvelopeRates,
                            kEnvelopeLevels,
                            Env::scaleoutlevel(voice->operator_level[op]) << 5,
                            0);
    }
    voice->pitch_env.set(kPitchEnvelopeRates, kPitchEnvelopeLevels);
    refresh_voice_patch(voice);
}

static void operator_envelope_values(const fm_voice_t *voice, int op,
                                     int rates[4], int levels[4])
{
    rates[0] = (int)voice->operator_env[op][0] + macro_delta(voice->env_attack, 18);
    rates[1] = (int)voice->operator_env[op][1] + macro_delta(voice->env_decay, 18);
    rates[2] = kEnvelopeRates[2];
    rates[3] = (int)voice->operator_env[op][3] + macro_delta(voice->env_release, 18);
    levels[0] = 99;
    levels[1] = 92;
    levels[2] = (int)voice->operator_env[op][2] + macro_delta(voice->env_sustain, 38);
    levels[3] = 0;
    for (int stage = 0; stage < 4; ++stage)
    {
        if (rates[stage] < 0) rates[stage] = 0;
        if (rates[stage] > 99) rates[stage] = 99;
        if (levels[stage] < 0) levels[stage] = 0;
        if (levels[stage] > 99) levels[stage] = 99;
    }
}

static void refresh_operator_envelope(fm_voice_t *voice, int op)
{
    if ((voice == nullptr) || (voice->active == 0U)) return;
    int rates[4];
    int levels[4];
    operator_envelope_values(voice, op, rates, levels);
    voice->env[op].update(rates, levels, operator_output_level(voice, op), 0);
}

static void refresh_all_envelopes(fm_voice_t *voice)
{
    for (int op = 0; op < kOperatorCount; ++op) refresh_operator_envelope(voice, op);
}

static void pitch_envelope_values(const fm_voice_t *voice, int rates[4], int levels[4])
{
    const int depth = (int)(voice->pitch_env_amount * 49.0f);
    const int rate = (int)(voice->pitch_env_time * 99.0f);
    for (int stage = 0; stage < 4; ++stage) rates[stage] = rate;
    levels[0] = 49 + depth;
    levels[1] = levels[2] = levels[3] = 49;
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
        int rates[4];
        int levels[4];
        operator_envelope_values(voice, op, rates, levels);
        voice->env[op].init(rates, levels, operator_output_level(voice, op), 0);
        voice->base_log_frequency[op] = operator_log_frequency(voice, note, op);
        voice->operators[op].freq = Freqlut::lookup(voice->base_log_frequency[op]);
        voice->operators[op].gain_out = 0;
        voice->env[op].keydown(true);
    }
#if FM_KERNEL_BENCH
    dx7_log_kernel_note_on(&voice->log_kernel, voice->sync != 0U);
    for (uint32_t op = 0U; op < (uint32_t)kOperatorCount; ++op)
        dx7_log_kernel_set_phase_increment(&voice->log_kernel,
                                           op,
                                           (uint32_t)voice->operators[op].freq << 8U);
#endif
    refresh_voice_patch(voice);
    int pitch_rates[4];
    int pitch_levels[4];
    pitch_envelope_values(voice, pitch_rates, pitch_levels);
    voice->pitch_env.set(pitch_rates, pitch_levels);
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
#if FM_KERNEL_BENCH
    dx7_log_kernel_init();
#endif
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

void brick6_fm_runtime_set_ratio(uint8_t instance_id, float value)
{
    if (valid_instance(instance_id) != 0U)
    {
        fm_voice_t *const voice = &g_fm_voice[instance_id];
        const float clamped = clamp_macro(value);
        if (voice->ratio == clamped) return;
        voice->ratio = clamped;
        refresh_voice_patch(voice);
        mark_parameters_changed(voice);
    }
}

void brick6_fm_runtime_set_algorithm(uint8_t instance_id, uint8_t algorithm)
{
    if (valid_instance(instance_id) != 0U)
    {
        fm_voice_t *const voice = &g_fm_voice[instance_id];
        const uint8_t clamped = clamp_algorithm(algorithm);
        if (voice->algorithm == clamped) return;
        voice->algorithm = clamped;
        refresh_voice_patch(voice);
        mark_parameters_changed(voice);
    }
}

void brick6_fm_runtime_set_feedback(uint8_t instance_id, uint8_t feedback)
{
    if (valid_instance(instance_id) != 0U)
    {
        fm_voice_t *const voice = &g_fm_voice[instance_id];
        const uint8_t clamped = (feedback > 7U) ? 7U : feedback;
        if (voice->feedback_amount == clamped) return;
        voice->feedback_amount = clamped;
        mark_parameters_changed(voice);
    }
}

void brick6_fm_runtime_set_sync(uint8_t instance_id, uint8_t enabled)
{
    if (valid_instance(instance_id) != 0U)
    {
        fm_voice_t *const voice = &g_fm_voice[instance_id];
        const uint8_t normalized = (enabled != 0U) ? 1U : 0U;
        if (voice->sync == normalized) return;
        voice->sync = normalized;
        mark_parameters_changed(voice);
    }
}

void brick6_fm_runtime_set_bright(uint8_t instance_id, float value)
{
    if (valid_instance(instance_id) != 0U)
    {
        fm_voice_t *const voice = &g_fm_voice[instance_id];
        const float clamped = clamp_macro(value);
        if (voice->bright == clamped) return;
        voice->bright = clamped;
        refresh_voice_patch(voice);
        mark_parameters_changed(voice);
    }
}

void brick6_fm_runtime_set_body(uint8_t instance_id, float value)
{
    if (valid_instance(instance_id) != 0U)
    {
        fm_voice_t *const voice = &g_fm_voice[instance_id];
        const float clamped = clamp_macro(value);
        if (voice->body == clamped) return;
        voice->body = clamped;
        refresh_voice_patch(voice);
        mark_parameters_changed(voice);
    }
}

void brick6_fm_runtime_set_detail(uint8_t instance_id, float value)
{
    if (valid_instance(instance_id) != 0U)
    {
        fm_voice_t *const voice = &g_fm_voice[instance_id];
        const float clamped = clamp_macro(value);
        if (voice->detail == clamped) return;
        voice->detail = clamped;
        refresh_voice_patch(voice);
        mark_parameters_changed(voice);
    }
}

void brick6_fm_runtime_set_metal(uint8_t instance_id, float value)
{
    if (valid_instance(instance_id) != 0U)
    {
        fm_voice_t *const voice = &g_fm_voice[instance_id];
        const float clamped = clamp_macro(value);
        if (voice->metal == clamped) return;
        voice->metal = clamped;
        refresh_voice_patch(voice);
        mark_parameters_changed(voice);
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
    const float next_attack = clamp_macro(attack);
    const float next_decay = clamp_macro(decay);
    const float next_sustain = clamp_macro(sustain);
    const float next_release = clamp_macro(release);
    if ((voice->env_attack == next_attack) && (voice->env_decay == next_decay)
            && (voice->env_sustain == next_sustain) && (voice->env_release == next_release))
        return;
    voice->env_attack = next_attack;
    voice->env_decay = next_decay;
    voice->env_sustain = next_sustain;
    voice->env_release = next_release;
    refresh_all_envelopes(voice);
    mark_parameters_changed(voice);
}

void brick6_fm_runtime_set_play(uint8_t instance_id,
                                float velocity,
                                float key_scaling,
                                float pitch_env,
                                float pitch_time)
{
    if (valid_instance(instance_id) == 0U)
        return;
    fm_voice_t *const voice = &g_fm_voice[instance_id];
    const float next_velocity = clamp_macro(velocity);
    const float next_key = clamp_macro(key_scaling);
    const float next_pitch_env = (pitch_env < -1.0f) ? -1.0f : ((pitch_env > 1.0f) ? 1.0f : pitch_env);
    const float next_pitch_time = clamp_macro(pitch_time);
    if ((voice->play_velocity == next_velocity) && (voice->play_key == next_key)
            && (voice->pitch_env_amount == next_pitch_env)
            && (voice->pitch_env_time == next_pitch_time))
        return;
    voice->play_velocity = next_velocity;
    voice->play_key = next_key;
    voice->pitch_env_amount = next_pitch_env;
    voice->pitch_env_time = next_pitch_time;
    refresh_voice_patch(voice);
    refresh_all_envelopes(voice);
    if (voice->active != 0U)
    {
        int rates[4];
        int levels[4];
        pitch_envelope_values(voice, rates, levels);
        voice->pitch_env.update(rates, levels);
    }
    mark_parameters_changed(voice);
}

void brick6_fm_runtime_set_operator(uint8_t instance_id,
                                    uint8_t operator_id,
                                    brick6_fm_operator_param_t param,
                                    float value)
{
    if ((valid_instance(instance_id) == 0U) || (operator_id >= kOperatorCount)
            || (param >= BRICK6_FM_OPERATOR_PARAM_COUNT))
        return;
    fm_voice_t *const voice = &g_fm_voice[instance_id];
    const int op = (int)brick_operator_to_msfa_index(operator_id);
    switch (param)
    {
        case BRICK6_FM_OPERATOR_LEVEL:
        {
            const uint8_t next = (uint8_t)((value < 0.0f) ? 0.0f : ((value > 99.0f) ? 99.0f : value + 0.5f));
            if (voice->operator_level[op] == next) return;
            voice->operator_level[op] = next;
            break;
        }
        case BRICK6_FM_OPERATOR_FREQ:
        {
            const float next = (value < 0.25f) ? 0.25f
                : ((value > 16.0f) ? 16.0f : value);
            if (voice->operator_frequency[op] == next) return;
            voice->operator_frequency[op] = next;
            break;
        }
        case BRICK6_FM_OPERATOR_DETUNE:
        {
            const int8_t next = (int8_t)((value < -7.0f) ? -7.0f
                : ((value > 7.0f) ? 7.0f
                : value + ((value < 0.0f) ? -0.5f : 0.5f)));
            if (voice->operator_detune[op] == next) return;
            voice->operator_detune[op] = next;
            break;
        }
        case BRICK6_FM_OPERATOR_ENV_ATTACK:
        case BRICK6_FM_OPERATOR_ENV_DECAY:
        case BRICK6_FM_OPERATOR_ENV_SUSTAIN:
        case BRICK6_FM_OPERATOR_ENV_RELEASE:
        {
            const uint8_t stage = (uint8_t)param - BRICK6_FM_OPERATOR_ENV_ATTACK;
            const uint8_t next = (uint8_t)((value < 0.0f) ? 0.0f : ((value > 99.0f) ? 99.0f : value + 0.5f));
            if (voice->operator_env[op][stage] == next) return;
            voice->operator_env[op][stage] = next;
            break;
        }
        case BRICK6_FM_OPERATOR_ON:
        {
            const uint8_t next = (value >= 0.5f) ? 1U : 0U;
            if (voice->operator_on[op] == next) return;
            voice->operator_on[op] = next;
            break;
        }
        case BRICK6_FM_OPERATOR_MODE:
        {
            const uint8_t next = (value >= 0.5f) ? 1U : 0U;
            if (voice->operator_mode[op] == next) return;
            voice->operator_mode[op] = next;
            break;
        }
        case BRICK6_FM_OPERATOR_VEL:
        {
            const uint8_t next = (uint8_t)(clamp_macro(value) * 7.0f + 0.5f);
            if (voice->operator_velocity[op] == next) return;
            voice->operator_velocity[op] = next;
            break;
        }
        case BRICK6_FM_OPERATOR_KEY:
        {
            const uint8_t next = (uint8_t)(clamp_macro(value) * 99.0f + 0.5f);
            if (voice->operator_key[op] == next) return;
            voice->operator_key[op] = next;
            break;
        }
        default:
            break;
    }
    if ((param == BRICK6_FM_OPERATOR_FREQ)
            || (param == BRICK6_FM_OPERATOR_DETUNE)
            || (param == BRICK6_FM_OPERATOR_MODE))
        refresh_operator_frequency(voice, op);
    if ((param == BRICK6_FM_OPERATOR_LEVEL)
            || ((param >= BRICK6_FM_OPERATOR_ENV_ATTACK)
                && (param <= BRICK6_FM_OPERATOR_ENV_RELEASE))
            || (param == BRICK6_FM_OPERATOR_VEL)
            || (param == BRICK6_FM_OPERATOR_KEY))
        refresh_operator_envelope(voice, op);
    mark_parameters_changed(voice);
}

void brick6_fm_runtime_sync_voice(uint8_t source_instance_id, uint8_t destination_instance_id)
{
    if ((valid_instance(source_instance_id) == 0U)
            || (valid_instance(destination_instance_id) == 0U))
        return;
    const fm_voice_t *const source = &g_fm_voice[source_instance_id];
    fm_voice_t *const destination = &g_fm_voice[destination_instance_id];
    if (destination->applied_source_generation == source->parameter_generation)
        return;
    const bool refresh_patch = (destination->ratio != source->ratio)
        || (destination->algorithm != source->algorithm)
        || (destination->bright != source->bright)
        || (destination->body != source->body)
        || (destination->detail != source->detail)
        || (destination->metal != source->metal);
    const bool refresh_global_env = (destination->env_attack != source->env_attack)
        || (destination->env_decay != source->env_decay)
        || (destination->env_sustain != source->env_sustain)
        || (destination->env_release != source->env_release);
    bool refresh_frequency[kOperatorCount] = {};
    bool refresh_envelope[kOperatorCount] = {};
    for (int op = 0; op < kOperatorCount; ++op)
    {
        refresh_frequency[op] = (destination->operator_frequency[op] != source->operator_frequency[op])
            || (destination->operator_detune[op] != source->operator_detune[op])
            || (destination->operator_mode[op] != source->operator_mode[op]);
        refresh_envelope[op] = (destination->operator_level[op] != source->operator_level[op])
            || (memcmp(destination->operator_env[op], source->operator_env[op], 4U) != 0)
            || (destination->operator_velocity[op] != source->operator_velocity[op])
            || (destination->operator_key[op] != source->operator_key[op]);
    }
    destination->ratio = source->ratio;
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
    destination->play_velocity = source->play_velocity;
    destination->play_key = source->play_key;
    destination->pitch_env_amount = source->pitch_env_amount;
    destination->pitch_env_time = source->pitch_env_time;
    memcpy(destination->operator_level, source->operator_level, sizeof(destination->operator_level));
    memcpy(destination->operator_frequency, source->operator_frequency, sizeof(destination->operator_frequency));
    memcpy(destination->operator_detune, source->operator_detune, sizeof(destination->operator_detune));
    memcpy(destination->operator_env, source->operator_env, sizeof(destination->operator_env));
    memcpy(destination->operator_on, source->operator_on, sizeof(destination->operator_on));
    memcpy(destination->operator_mode, source->operator_mode, sizeof(destination->operator_mode));
    memcpy(destination->operator_velocity, source->operator_velocity, sizeof(destination->operator_velocity));
    memcpy(destination->operator_key, source->operator_key, sizeof(destination->operator_key));
    if (refresh_patch)
        refresh_voice_patch(destination);
    else
        for (int op = 0; op < kOperatorCount; ++op)
            if (refresh_frequency[op]) refresh_operator_frequency(destination, op);
    if (refresh_global_env)
        refresh_all_envelopes(destination);
    else
        for (int op = 0; op < kOperatorCount; ++op)
            if (refresh_envelope[op]) refresh_operator_envelope(destination, op);
    if (destination->active != 0U)
    {
        int rates[4];
        int levels[4];
        pitch_envelope_values(destination, rates, levels);
        destination->pitch_env.update(rates, levels);
    }
    destination->parameter_generation = source->parameter_generation;
    destination->applied_source_generation = source->parameter_generation;
}

void brick6_fm_runtime_sync_voice_if_needed(uint8_t source_instance_id,
                                            uint8_t destination_instance_id)
{
    if ((valid_instance(source_instance_id) == 0U)
            || (valid_instance(destination_instance_id) == 0U))
        return;
    if (g_fm_voice[destination_instance_id].applied_source_generation
            != g_fm_voice[source_instance_id].parameter_generation)
        brick6_fm_runtime_sync_voice(source_instance_id, destination_instance_id);
}

void brick6_fm_runtime_move_voice(uint8_t source_instance_id, uint8_t destination_instance_id)
{
    if ((valid_instance(source_instance_id) == 0U)
            || (valid_instance(destination_instance_id) == 0U)
            || (source_instance_id == destination_instance_id))
        return;
    g_fm_voice[destination_instance_id] = g_fm_voice[source_instance_id];
    reset_voice(&g_fm_voice[source_instance_id]);
}

uint8_t brick6_fm_runtime_voice_is_active(uint8_t instance_id)
{
    return (valid_instance(instance_id) != 0U) ? g_fm_voice[instance_id].active : 0U;
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

    const int32_t pitch_log_frequency = voice->pitch_env.getsample(frames);
    for (int op = 0; op < kOperatorCount; ++op)
    {
        voice->operators[op].level_in = voice->env[op].getsample(frames);
        voice->operators[op].level_in += voice->operator_level_offset[op];
        if (voice->operator_on[op] == 0U)
            voice->operators[op].level_in = 0;
        voice->operators[op].freq = Freqlut::lookup(voice->base_log_frequency[op]
                                                     + pitch_log_frequency);
    }
#if FM_KERNEL_BENCH
    if (voice->algorithm == 0U)
    {
        for (uint32_t op = 0U; op < (uint32_t)kOperatorCount; ++op)
            dx7_log_kernel_prepare_operator(&voice->log_kernel,
                                             op,
                                             voice->operators[op].level_in,
                                             (uint32_t)voice->operators[op].freq << 8U,
                                             frames);
        dx7_log_kernel_render_algorithm_1(&voice->log_kernel,
                                          log_feedback_shift(voice->feedback_amount),
                                          out_mono,
                                          frames);
    }
    else
#endif
    {
        int32_t block[BRICK6_FM_RENDER_BLOCK] = { 0 };
        g_fm_modern.render(block,
                           voice->operators,
                           voice->algorithm,
                           voice->feedback,
                           feedback_shift(voice->feedback_amount),
                           (int)frames);

        constexpr float kOutputScale = 0.125f / (float)kQ24;
        for (uint32_t i = 0U; i < frames; ++i)
            out_mono[i] = (float)block[i] * kOutputScale;
    }

    uint8_t carrier_active = 0U;
    for (int op = 0; op < kOperatorCount; ++op)
    {
        if (operator_is_carrier(voice->algorithm, op) && voice->env[op].isActive())
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
