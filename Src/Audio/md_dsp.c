#include "Audio/md_dsp.h"

#include <stddef.h>

static const int16_t g_md_sine_lut[MD_DSP_SINE_LUT_SIZE + 1U] = {
#include "md_sine_lut.inc"
};

static float md_clampf(float value, float lo, float hi)
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

uint32_t md_phase_increment_from_hz(float frequency_hz, float sample_rate)
{
    if ((frequency_hz <= 0.0f) || (sample_rate <= 1.0f))
    {
        return 0U;
    }
    const double increment = ((double)frequency_hz * 4294967296.0) / (double)sample_rate;
    if (increment >= 4294967295.0)
    {
        return 0xFFFFFFFFUL;
    }
    return (uint32_t)(increment + 0.5);
}

void md_phase_set_frequency(md_phase_t *osc, float frequency_hz, float sample_rate)
{
    if (osc != NULL)
    {
        osc->increment = md_phase_increment_from_hz(frequency_hz, sample_rate);
    }
}

void md_phase_reset(md_phase_t *osc, uint32_t phase)
{
    if (osc != NULL)
    {
        osc->phase = phase;
    }
}

float md_phase_sine_next(md_phase_t *osc)
{
    if (osc == NULL)
    {
        return 0.0f;
    }
    const uint32_t phase = osc->phase;
    osc->phase += osc->increment;
    const uint32_t index = phase >> (32U - MD_DSP_SINE_LUT_BITS);
    const uint32_t fraction = (phase >> (32U - MD_DSP_SINE_LUT_BITS - 16U)) & 0xFFFFU;
    const int32_t a = g_md_sine_lut[index];
    const int32_t b = g_md_sine_lut[index + 1U];
    const int32_t sample = a + (int32_t)(((int64_t)(b - a) * (int64_t)fraction) >> 16U);
    return (float)sample * (1.0f / 32768.0f);
}

float md_phase_square_next(md_phase_t *osc)
{
    if (osc == NULL)
    {
        return 0.0f;
    }
    const uint32_t phase = osc->phase;
    osc->phase += osc->increment;
    return ((phase & 0x80000000UL) == 0U) ? 1.0f : -1.0f;
}

float md_decay_coefficient(float seconds, float sample_rate)
{
    if ((seconds <= 0.0f) || (sample_rate <= 1.0f))
    {
        return 0.0f;
    }
    const float x = 1.0f / (seconds * sample_rate);
    return 1.0f / (1.0f + x);
}

void md_decay_env_prepare(md_decay_env_t *env, float seconds, float sample_rate)
{
    if (env != NULL)
    {
        env->coefficient = md_decay_coefficient(seconds, sample_rate);
        env->end_threshold = 1.0e-5f;
    }
}

void md_decay_env_trigger(md_decay_env_t *env, float level)
{
    if (env != NULL)
    {
        env->value = md_clampf(level, 0.0f, 1.0f);
        env->active = (env->value > 0.0f) ? 1U : 0U;
    }
}

float md_decay_env_process(md_decay_env_t *env)
{
    if ((env == NULL) || (env->active == 0U))
    {
        return 0.0f;
    }
    const float output = env->value;
    env->value *= env->coefficient;
    if (env->value <= env->end_threshold)
    {
        env->value = 0.0f;
        env->active = 0U;
    }
    return output;
}

void md_rng_seed(md_rng_t *rng, uint32_t seed)
{
    if (rng != NULL)
    {
        rng->state = (seed != 0U) ? seed : 0x6D2B79F5UL;
    }
}

uint32_t md_rng_next_u32(md_rng_t *rng)
{
    if (rng == NULL)
    {
        return 0U;
    }
    uint32_t x = rng->state;
    if (x == 0U)
    {
        x = 0x6D2B79F5UL;
    }
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    rng->state = x;
    return x;
}

float md_rng_next_bipolar(md_rng_t *rng)
{
    const int32_t sample = (int32_t)(md_rng_next_u32(rng) >> 8U) - 8388608;
    return (float)sample * (1.0f / 8388608.0f);
}

void md_hpf_prepare(md_hpf_t *filter, float coefficient)
{
    if (filter != NULL)
    {
        filter->coefficient = md_clampf(coefficient, 0.0f, 0.999999f);
    }
}

void md_hpf_reset(md_hpf_t *filter)
{
    if (filter != NULL)
    {
        filter->x1 = 0.0f;
        filter->y1 = 0.0f;
    }
}

float md_hpf_process(md_hpf_t *filter, float input)
{
    if (filter == NULL)
    {
        return input;
    }
    const float output = input - filter->x1 + (filter->coefficient * filter->y1);
    filter->x1 = input;
    filter->y1 = output;
    return output;
}

void md_lpf_prepare(md_lpf_t *filter, float coefficient)
{
    if (filter != NULL)
    {
        filter->coefficient = md_clampf(coefficient, 0.0f, 1.0f);
    }
}

void md_lpf_reset(md_lpf_t *filter)
{
    if (filter != NULL)
    {
        filter->state = 0.0f;
    }
}

float md_lpf_process(md_lpf_t *filter, float input)
{
    if (filter == NULL)
    {
        return input;
    }
    filter->state += filter->coefficient * (input - filter->state);
    return filter->state;
}

float md_clip(float input, float drive)
{
    const float driven = input * md_clampf(drive, 1.0f, 16.0f);
    return md_clampf(driven, -1.0f, 1.0f);
}

float md_mix2(float a, float b, float balance)
{
    const float mix = md_clampf(balance, 0.0f, 1.0f);
    return a + ((b - a) * mix);
}

void md_retrigger_fade_begin(md_retrigger_fade_t *fade, float previous_tail, uint16_t samples)
{
    if (fade != NULL)
    {
        fade->previous_tail = previous_tail;
        fade->gain = (samples != 0U) ? 1.0f : 0.0f;
        fade->decrement = (samples != 0U) ? (1.0f / (float)samples) : 1.0f;
    }
}

float md_retrigger_fade_process(md_retrigger_fade_t *fade, float fresh)
{
    if ((fade == NULL) || (fade->gain <= 0.0f))
    {
        return fresh;
    }
    const float output = fresh + (fade->previous_tail * fade->gain);
    fade->gain -= fade->decrement;
    if (fade->gain < 0.0f)
    {
        fade->gain = 0.0f;
    }
    return output;
}
