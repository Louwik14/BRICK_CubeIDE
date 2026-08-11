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
    uint8_t mode;
    uint8_t algorithm;
    uint8_t feedback_amount;
    uint8_t sync;
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

static int32_t note_log_frequency(uint8_t note, uint8_t ratio)
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
        voice->env[op].init(kEnvelopeRates,
                            kEnvelopeLevels,
                            (int)kOperatorLevels[op] << 5,
                            0);
        voice->base_log_frequency[op] = note_log_frequency(note, kOperatorRatios[op]);
        voice->operators[op].freq = Freqlut::lookup(voice->base_log_frequency[op]);
        voice->operators[op].gain_out = 0;
        voice->env[op].keydown(true);
    }
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
        g_fm_voice[instance_id].algorithm = clamp_algorithm(algorithm);
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
