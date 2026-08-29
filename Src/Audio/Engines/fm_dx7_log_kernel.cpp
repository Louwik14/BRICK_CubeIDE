#include "fm_dx7_log_kernel.h"

#include <string.h>
#include "Platform/memory_layout.h"

namespace
{
constexpr uint32_t kOperatorCount = 6U;
constexpr uint32_t kLogFractionBits = 10U;
constexpr uint32_t kLogTableSize = 1U << kLogFractionBits;
constexpr uint32_t kLogMaximum = (1U << 14U) - 1U;
/* Operator samples are signed Q14; the phase accumulator is Q32/cycle. */
constexpr uint32_t kModulationToPhaseShift = 32U - 14U;
constexpr int32_t kSilentAttenuationQ16 = (int32_t)kLogMaximum << 16;
constexpr uint32_t kCarrierCompensation = 1U << kLogFractionBits;
constexpr float kOutputScale = 1.0f / 32768.0f;

/* Four KiB total in Flash; the 4 KiB working set is D-cache friendly on H743. */
static const uint16_t g_log_sine[kLogTableSize] = {
#include "fm_dx7_log_sine_1024.inc"
};
static const uint16_t g_exp_mantissa[kLogTableSize] = {
#include "fm_dx7_exp_mantissa_1024.inc"
};

static inline __attribute__((always_inline)) int32_t lookup_wave(uint32_t phase,
                                                                  int32_t attenuation_q16,
                                                                  uint32_t compensation)
{
    const uint32_t phase12 = phase >> 20U;
    const uint32_t reverse_mask = 0U - ((phase12 >> 10U) & 1U);
    const uint32_t index = (phase12 & 0x3ffU) ^ (reverse_mask & 0x3ffU);
    uint32_t log_value = (uint32_t)g_log_sine[index]
        + ((uint32_t)attenuation_q16 >> 16U)
        + compensation;
    if (log_value > kLogMaximum)
        log_value = kLogMaximum;

    const uint32_t exponent = log_value >> kLogFractionBits;
    const uint32_t fraction = log_value & (kLogTableSize - 1U);
    const int32_t magnitude = (int32_t)(g_exp_mantissa[fraction] >> exponent);
    return ((phase12 & 0x800U) != 0U) ? -magnitude : magnitude;
}

static inline __attribute__((always_inline)) int32_t render_operator(
    volatile dx7_log_kernel_operator_t *op,
    int32_t modulation,
    uint32_t compensation)
{
    const int32_t increment = (int32_t)op->phase_increment
        + op->phase_increment_delta;
    const int32_t attenuation = op->attenuation_q16
        + op->attenuation_delta_q16;
    const uint32_t phase_base = op->phase;
    const uint32_t phase = phase_base
        + ((uint32_t)modulation << kModulationToPhaseShift);

    op->phase = phase_base + (uint32_t)increment;
    op->phase_increment = (uint32_t)increment;
    op->attenuation_q16 = attenuation;
    return lookup_wave(phase, attenuation, compensation);
}

static inline __attribute__((always_inline)) void render_sample(
    dx7_log_kernel_voice_t *voice,
    uint32_t feedback_shift,
    int32_t &feedback_0,
    int32_t &feedback_1,
    float *output)
{
    const int32_t feedback = (feedback_0 + feedback_1) >> feedback_shift;
    const int32_t op6 = render_operator(&voice->operators[0], feedback, 0U);
    feedback_0 = feedback_1;
    feedback_1 = op6;
    const int32_t op5 = render_operator(&voice->operators[1], op6, 0U);
    const int32_t op4 = render_operator(&voice->operators[2], op5, 0U);
    const int32_t op3 = render_operator(&voice->operators[3], op4, kCarrierCompensation);
    const int32_t op2 = render_operator(&voice->operators[4], 0, 0U);
    const int32_t op1 = render_operator(&voice->operators[5], op2, kCarrierCompensation);
    *output = (float)(op3 + op1) * kOutputScale;
}
}

void dx7_log_kernel_init(void)
{
}

void dx7_log_kernel_reset(dx7_log_kernel_voice_t *voice)
{
    if (voice == nullptr)
        return;
    memset(voice, 0, sizeof(*voice));
    for (uint32_t op = 0U; op < kOperatorCount; ++op)
    {
        voice->operators[op].attenuation_q16 = kSilentAttenuationQ16;
        voice->attenuation_target_q16[op] = kSilentAttenuationQ16;
    }
}

void dx7_log_kernel_note_on(dx7_log_kernel_voice_t *voice, bool sync)
{
    if (voice == nullptr)
        return;
    if (sync)
    {
        for (uint32_t op = 0U; op < kOperatorCount; ++op)
            voice->operators[op].phase = 0U;
        memset(voice->feedback, 0, sizeof(voice->feedback));
    }
    for (uint32_t op = 0U; op < kOperatorCount; ++op)
    {
        voice->operators[op].attenuation_q16 = kSilentAttenuationQ16;
        voice->operators[op].attenuation_delta_q16 = 0;
        voice->attenuation_target_q16[op] = kSilentAttenuationQ16;
    }
}

void dx7_log_kernel_initialize_held(dx7_log_kernel_voice_t *voice)
{
    if (voice == nullptr) return;
    for (uint32_t op = 0U; op < kOperatorCount; ++op)
    {
        voice->operators[op].attenuation_q16 = kSilentAttenuationQ16;
        voice->operators[op].attenuation_delta_q16 = 0;
        voice->attenuation_target_q16[op] = kSilentAttenuationQ16;
    }
}

void dx7_log_kernel_set_phase_increment(dx7_log_kernel_voice_t *voice,
                                        uint32_t operator_index,
                                        uint32_t phase_increment)
{
    if ((voice == nullptr) || (operator_index >= kOperatorCount))
        return;
    voice->operators[operator_index].phase_increment = phase_increment;
    voice->operators[operator_index].phase_increment_delta = 0;
    voice->phase_increment_target[operator_index] = phase_increment;
}

void dx7_log_kernel_prepare_operator(dx7_log_kernel_voice_t *voice,
                                     uint32_t operator_index,
                                     int32_t level_q24,
                                     uint32_t phase_increment,
                                     uint32_t frames)
{
    if ((voice == nullptr) || (operator_index >= kOperatorCount) || (frames == 0U))
        return;

    /* MSFA gain is 2^(level-14). Carrier compensation supplies the final /2. */
    const int32_t level_log_units = level_q24 >> (24 - kLogFractionBits);
    int32_t attenuation_log_units = (15 << kLogFractionBits) - level_log_units;
    if (attenuation_log_units < 0)
        attenuation_log_units = 0;
    if (attenuation_log_units > (int32_t)kLogMaximum)
        attenuation_log_units = (int32_t)kLogMaximum;
    const int32_t attenuation_target = attenuation_log_units << 16;
    dx7_log_kernel_operator_t *const op = &voice->operators[operator_index];
    const int32_t attenuation_current = op->attenuation_q16;
    voice->attenuation_target_q16[operator_index] = attenuation_target;
    op->attenuation_delta_q16 =
        (attenuation_target - attenuation_current) / (int32_t)frames;

    const int32_t increment_difference = (int32_t)(phase_increment
        - op->phase_increment);
    voice->phase_increment_target[operator_index] = phase_increment;
    op->phase_increment_delta =
        increment_difference / (int32_t)frames;
}

ITCM_TEXT void __attribute__((hot)) dx7_log_kernel_render_algorithm_1(
    dx7_log_kernel_voice_t *voice,
    uint32_t feedback_shift,
    float *output,
    uint32_t frames)
{
    if ((voice == nullptr) || (output == nullptr) || (frames == 0U))
        return;

    int32_t feedback_0 = voice->feedback[0];
    int32_t feedback_1 = voice->feedback[1];
    float *cursor = output;
    uint32_t pairs = frames >> 1U;
    while (pairs-- != 0U)
    {
        render_sample(voice, feedback_shift, feedback_0, feedback_1, cursor++);
        render_sample(voice, feedback_shift, feedback_0, feedback_1, cursor++);
    }
    if ((frames & 1U) != 0U)
        render_sample(voice, feedback_shift, feedback_0, feedback_1, cursor);
    voice->feedback[0] = feedback_0;
    voice->feedback[1] = feedback_1;

    for (uint32_t op = 0U; op < kOperatorCount; ++op)
    {
        voice->operators[op].phase_increment = voice->phase_increment_target[op];
        voice->operators[op].attenuation_q16 = voice->attenuation_target_q16[op];
    }
}
