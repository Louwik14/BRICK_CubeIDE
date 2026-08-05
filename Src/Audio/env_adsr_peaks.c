#include "env_adsr_peaks.h"

#include <math.h>
#include <stddef.h>

#define ENV_ADSR_Q15_MAX 32767
#define ENV_ADSR_PHASE_MAX 0xFFFFFFFFu
#define ENV_ADSR_MAX_SEGMENT_SECONDS 30u
#define ENV_ADSR_RELEASE_END_RATIO (1.0f / 65536.0f)

static uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static uint32_t phase_increment_from_time(const env_adsr_peaks_t *env, uint16_t time)
{
    uint32_t scaled = 0u;
#if defined(__SIZEOF_INT128__)
    const uint64_t x = (uint64_t)time;
    const __uint128_t scaled_wide = ((__uint128_t)(x * x * x) * (__uint128_t)env->max_segment_samples) >> 48;
    scaled = (scaled_wide > (__uint128_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)scaled_wide;
#else
    /* Fallback without 128-bit arithmetic: keep cubic law monotonic and overflow-safe. */
    const double t = (double)time * (1.0 / 65535.0);
    const double shaped = t * t * t;
    const double wide = shaped * (double)env->max_segment_samples;
    if(wide > (double)UINT32_MAX)
        scaled = UINT32_MAX;
    else if(wide > 0.0)
        scaled = (uint32_t)wide;
#endif
    const uint32_t duration_samples = 1u + scaled;

    if(duration_samples <= 1u)
        return ENV_ADSR_PHASE_MAX;

    uint32_t increment = (uint32_t)(((uint64_t)1u << 32) / (uint64_t)duration_samples);
    if(increment == 0u)
        increment = 1u;
    return increment;
}

static void refresh_increments(env_adsr_peaks_t *env)
{
    env->attack_increment = phase_increment_from_time(env, env->attack);
    env->decay_increment = phase_increment_from_time(env, env->decay);
    env->release_increment = phase_increment_from_time(env, env->release);
}

static uint16_t phase_curve_linear(uint32_t phase)
{
    return (uint16_t)(phase >> 16);
}

static uint16_t phase_curve_quartic(uint32_t phase)
{
    const uint32_t x = phase >> 16;
    const uint32_t x2 = (x * x) >> 16;
    const uint32_t x4 = (x2 * x2) >> 16;
    return (uint16_t)x4;
}

static uint16_t phase_curve_exponential(uint32_t phase)
{
    const uint32_t x = phase >> 16;
    const uint32_t inv = 65535u - x;
    const uint32_t inv2 = (inv * inv) >> 16;
    const uint32_t inv4 = (inv2 * inv2) >> 16;
    return (uint16_t)(65535u - inv4);
}

static int16_t interpolate_q15(int16_t a, int16_t b, uint16_t t)
{
    const int32_t delta = (int32_t)b - (int32_t)a;
    return (int16_t)((int32_t)a + ((delta * (int32_t)t) >> 16));
}

static void start_attack(env_adsr_peaks_t *env, bool hard_reset)
{
    env->start_value = (hard_reset || env->stage == ENV_ADSR_PEAKS_STAGE_IDLE) ? 0 : env->value;
    env->target_value = ENV_ADSR_Q15_MAX;
    env->phase = 0;
    env->phase_increment = env->attack_increment;
    env->stage = ENV_ADSR_PEAKS_STAGE_ATTACK;
}

void env_adsr_peaks_init(env_adsr_peaks_t *env, float sample_rate)
{
    const float sr = (sample_rate > 1.0f) ? sample_rate : 48000.0f;
    env->max_segment_samples = (uint32_t)(sr * (float)ENV_ADSR_MAX_SEGMENT_SECONDS);
    if(env->max_segment_samples == 0u)
        env->max_segment_samples = 1u;

    env->attack = 0u;
    env->decay = 8192u;
    env->sustain = 16384u;
    env->release = 32767u;
    env->hard_reset = false;

    refresh_increments(env);
    env_adsr_peaks_reset(env);
}

void env_adsr_peaks_reset(env_adsr_peaks_t *env)
{
    env->gate_high = false;
    env->phase = 0u;
    env->phase_increment = 0u;
    env->start_value = 0;
    env->target_value = 0;
    env->value = 0;
    env->release_level = 0.0f;
    env->release_coefficient = 1.0f;
    env->stage = ENV_ADSR_PEAKS_STAGE_IDLE;
}

void env_adsr_peaks_set_attack(env_adsr_peaks_t *env, uint16_t attack)
{
    env->attack = attack;
    env->attack_increment = phase_increment_from_time(env, env->attack);
}

void env_adsr_peaks_set_decay(env_adsr_peaks_t *env, uint16_t decay)
{
    env->decay = decay;
    env->decay_increment = phase_increment_from_time(env, env->decay);
}

void env_adsr_peaks_set_sustain(env_adsr_peaks_t *env, uint16_t sustain)
{
    env->sustain = clamp_u16(sustain, 0u, ENV_ADSR_Q15_MAX);
}

void env_adsr_peaks_set_release(env_adsr_peaks_t *env, uint16_t release)
{
    env->release = release;
    env->release_increment = phase_increment_from_time(env, env->release);
}

void env_adsr_peaks_gate_on(env_adsr_peaks_t *env)
{
    env->gate_high = true;
    start_attack(env, env->hard_reset);
}

void env_adsr_peaks_gate_off(env_adsr_peaks_t *env)
{
    env->gate_high = false;
    if(env->stage != ENV_ADSR_PEAKS_STAGE_IDLE && env->stage != ENV_ADSR_PEAKS_STAGE_RELEASE)
    {
        env->start_value = env->value;
        env->target_value = 0;
        env->phase = 0;
        env->phase_increment = env->release_increment;
        env->release_level = (float)env->value;
        const uint32_t duration_samples =
            (env->phase_increment != 0u)
                ? ((UINT32_MAX / env->phase_increment) + 1u)
                : 1u;
        const float multiplier = powf(ENV_ADSR_RELEASE_END_RATIO,
                                      1.0f / (float)duration_samples);
        env->release_coefficient = 1.0f - multiplier;
        if(!(env->release_coefficient > 0.0f))
            env->release_coefficient = 1.0f;
        else if(env->release_coefficient > 1.0f)
            env->release_coefficient = 1.0f;
        env->stage = ENV_ADSR_PEAKS_STAGE_RELEASE;
    }
}

void env_adsr_peaks_retrigger(env_adsr_peaks_t *env, bool hard_reset)
{
    env->hard_reset = hard_reset;
    env->gate_high = true;
    start_attack(env, hard_reset);
}

int16_t env_adsr_peaks_process_step(env_adsr_peaks_t *env)
{
    if(env->stage == ENV_ADSR_PEAKS_STAGE_IDLE)
        return 0;

    if(env->stage == ENV_ADSR_PEAKS_STAGE_SUSTAIN)
    {
        env->value = (int16_t)env->sustain;
        return env->value;
    }

    if(env->stage == ENV_ADSR_PEAKS_STAGE_RELEASE)
    {
        const uint32_t previous_phase = env->phase;
        env->phase += env->phase_increment;
        env->release_level += env->release_coefficient * (0.0f - env->release_level);

        if((env->phase < previous_phase) || !(env->release_level > 0.0f))
        {
            env->stage = ENV_ADSR_PEAKS_STAGE_IDLE;
            env->value = 0;
            env->release_level = 0.0f;
            env->phase = 0u;
            env->phase_increment = 0u;
            return 0;
        }

        env->value = (int16_t)env->release_level;
        /* Q15 zero is terminal only after a real RELEASE phase advance.  A
         * zero value in ATTACK/DECAY/SUSTAIN is not an end condition. */
        if ((env->phase != previous_phase) && (env->value <= 0))
        {
            env->stage = ENV_ADSR_PEAKS_STAGE_IDLE;
            env->release_level = 0.0f;
            env->phase = 0u;
            env->phase_increment = 0u;
            return 0;
        }
        return env->value;
    }

    const uint32_t previous_phase = env->phase;
    env->phase += env->phase_increment;

    uint16_t curve = 0;
    if(env->stage == ENV_ADSR_PEAKS_STAGE_ATTACK)
        curve = phase_curve_quartic(env->phase);
    else if(env->stage == ENV_ADSR_PEAKS_STAGE_DECAY)
        curve = phase_curve_exponential(env->phase);
    else
        curve = phase_curve_linear(env->phase);

    env->value = interpolate_q15(env->start_value, env->target_value, curve);

    if(env->phase < previous_phase)
    {
        if(env->stage == ENV_ADSR_PEAKS_STAGE_ATTACK)
        {
            env->start_value = ENV_ADSR_Q15_MAX;
            env->target_value = (int16_t)env->sustain;
            env->phase = 0;
            env->phase_increment = env->decay_increment;
            env->stage = ENV_ADSR_PEAKS_STAGE_DECAY;
            env->value = ENV_ADSR_Q15_MAX;
        }
        else if(env->stage == ENV_ADSR_PEAKS_STAGE_DECAY)
        {
            if(env->gate_high)
            {
                env->stage = ENV_ADSR_PEAKS_STAGE_SUSTAIN;
                env->value = (int16_t)env->sustain;
            }
            else
            {
                env->start_value = (int16_t)env->sustain;
                env->target_value = 0;
                env->phase = 0;
                env->phase_increment = env->release_increment;
                env->stage = ENV_ADSR_PEAKS_STAGE_RELEASE;
                env->value = (int16_t)env->sustain;
            }
        }
    }

    return env->value;
}

int16_t env_adsr_peaks_process_advance(env_adsr_peaks_t *env,
                                       uint32_t steps,
                                       int16_t *first_value)
{
    if(steps == 0u)
    {
        if(first_value != NULL)
            *first_value = env->value;
        return env->value;
    }

    if(first_value != NULL)
    {
        *first_value = env_adsr_peaks_process_step(env);
        steps--;
        if(steps == 0u)
            return env->value;
    }

    while(steps > 0u)
    {
        if(env->stage == ENV_ADSR_PEAKS_STAGE_IDLE)
        {
            return 0;
        }

        if(env->stage == ENV_ADSR_PEAKS_STAGE_SUSTAIN)
        {
            env->value = (int16_t)env->sustain;
            return env->value;
        }

        if(env->stage == ENV_ADSR_PEAKS_STAGE_RELEASE)
        {
            do
            {
                (void)env_adsr_peaks_process_step(env);
                steps--;
            } while((steps > 0u) && (env->stage == ENV_ADSR_PEAKS_STAGE_RELEASE));
            continue;
        }

        if(((env->stage != ENV_ADSR_PEAKS_STAGE_ATTACK)
                && (env->stage != ENV_ADSR_PEAKS_STAGE_DECAY)
                && (env->stage != ENV_ADSR_PEAKS_STAGE_RELEASE))
                || (env->phase_increment == 0u))
        {
            do
            {
                (void)env_adsr_peaks_process_step(env);
                steps--;
            } while(steps > 0u);
            return env->value;
        }

        const uint32_t steps_before_wrap =
                (UINT32_MAX - env->phase) / env->phase_increment;

        if(steps <= steps_before_wrap)
        {
            env->phase += env->phase_increment * steps;

            uint16_t curve = 0u;
            if(env->stage == ENV_ADSR_PEAKS_STAGE_ATTACK)
                curve = phase_curve_quartic(env->phase);
            else
                curve = phase_curve_exponential(env->phase);

            env->value = interpolate_q15(env->start_value, env->target_value, curve);
            return env->value;
        }

        const uint32_t steps_to_wrap = steps_before_wrap + 1u;
        env->phase += env->phase_increment * steps_to_wrap;
        steps -= steps_to_wrap;

        if(env->stage == ENV_ADSR_PEAKS_STAGE_ATTACK)
        {
            env->start_value = ENV_ADSR_Q15_MAX;
            env->target_value = (int16_t)env->sustain;
            env->phase = 0u;
            env->phase_increment = env->decay_increment;
            env->stage = ENV_ADSR_PEAKS_STAGE_DECAY;
            env->value = ENV_ADSR_Q15_MAX;
        }
        else if(env->stage == ENV_ADSR_PEAKS_STAGE_DECAY)
        {
            if(env->gate_high)
            {
                env->stage = ENV_ADSR_PEAKS_STAGE_SUSTAIN;
                env->value = (int16_t)env->sustain;
            }
            else
            {
                env->start_value = (int16_t)env->sustain;
                env->target_value = 0;
                env->phase = 0u;
                env->phase_increment = env->release_increment;
                env->stage = ENV_ADSR_PEAKS_STAGE_RELEASE;
                env->value = (int16_t)env->sustain;
            }
        }
        else
        {
            env->stage = ENV_ADSR_PEAKS_STAGE_IDLE;
            env->value = 0;
            env->phase = 0u;
            env->phase_increment = 0u;
        }
    }

    return env->value;
}

int16_t env_adsr_peaks_value(const env_adsr_peaks_t *env)
{
    return env->value;
}

env_adsr_peaks_stage_t env_adsr_peaks_stage(const env_adsr_peaks_t *env)
{
    return (env_adsr_peaks_stage_t)env->stage;
}
