#include "vca_env.h"

#include <math.h>

static uint32_t vca_env_time_to_samples(const vca_env_t *env, float time_seconds)
{
    if ((env == NULL) || (time_seconds <= 0.0f))
    {
        return 1U;
    }

    const float samples = roundf(time_seconds * env->sample_rate);
    if (samples < 1.0f)
    {
        return 1U;
    }
    if (samples >= 4294967295.0f)
    {
        return UINT32_MAX;
    }
    return (uint32_t)samples;
}

static float vca_env_clamp_level(float level)
{
    if (level <= 0.0f)
    {
        return 0.0f;
    }
    if (level >= 1.0f)
    {
        return 1.0f;
    }
    return level;
}

static float vca_env_coefficient(float time_seconds,
                                 float sample_rate,
                                 float ratio)
{
    if ((time_seconds <= 0.0f) || (sample_rate <= 0.0f) || (ratio <= 0.0f))
    {
        return 1.0f;
    }
    const float samples = time_seconds * sample_rate;
    return 1.0f - expf(logf(ratio) / samples);
}

static void vca_env_prepare_attack(vca_env_t *env)
{
    env->attack_target = 1.01f;
    env->samples_remaining = vca_env_time_to_samples(env, env->attack_time);

    if (env->type == VCA_ENV_TYPE_LINEAR)
    {
        env->linear_increment = (1.0f - env->level)
                              / (float)env->samples_remaining;
    }
}

static void vca_env_prepare_decay(vca_env_t *env)
{
    env->samples_remaining = vca_env_time_to_samples(env, env->decay_time);

    if (env->type == VCA_ENV_TYPE_LINEAR)
    {
        env->linear_increment = (env->sustain - env->level)
                              / (float)env->samples_remaining;
    }
}

static void vca_env_prepare_release(vca_env_t *env)
{
    env->release_target = -0.01f;
    env->samples_remaining = vca_env_time_to_samples(env, env->release_time);

    if (env->level <= 0.0f)
    {
        env->release_coefficient = 1.0f;
    }
    else
    {
        const float ratio = (-env->release_target)
                          / (env->level - env->release_target);
        env->release_coefficient = vca_env_coefficient(
            env->release_time,
            env->sample_rate,
            ratio);
    }

    if (env->type == VCA_ENV_TYPE_LINEAR)
    {
        env->linear_increment = -env->level
                              / (float)env->samples_remaining;
    }
}

static void vca_env_prepare_stage(vca_env_t *env)
{
    switch (env->stage)
    {
        case VCA_ENV_ATTACK:
            vca_env_prepare_attack(env);
            break;
        case VCA_ENV_DECAY:
            vca_env_prepare_decay(env);
            break;
        case VCA_ENV_RELEASE:
            vca_env_prepare_release(env);
            break;
        default:
            break;
    }
}

void vca_env_init(vca_env_t *env, float sample_rate)
{
    if (env == NULL)
    {
        return;
    }
    env->type = VCA_ENV_TYPE_DAISY;
    env->sample_rate = sample_rate;
    env->attack_time = 0.001f;
    env->decay_time = 0.001f;
    env->release_time = 0.001f;
    env->sustain = 1.0f;
    env->attack_target = 1.01f;
    env->release_target = -0.01f;
    env->attack_coefficient = vca_env_coefficient(
        env->attack_time,
        env->sample_rate,
        1.0f - (1.0f / env->attack_target));
    env->decay_coefficient = vca_env_coefficient(
        env->decay_time,
        env->sample_rate,
        expf(-1.0f));
    env->release_coefficient = 1.0f;
    vca_env_reset(env);
}

void vca_env_reset(vca_env_t *env)
{
    if (env == NULL)
    {
        return;
    }
    env->stage = VCA_ENV_IDLE;
    env->level = 0.0f;
    env->linear_increment = 0.0f;
    env->samples_remaining = 0U;
    env->gate = false;
    vca_env_prepare_stage(env);
}

void vca_env_set_type(vca_env_t *env, vca_env_type_t type)
{
    if (env != NULL)
    {
        env->type = (type == VCA_ENV_TYPE_LINEAR)
                  ? VCA_ENV_TYPE_LINEAR : VCA_ENV_TYPE_DAISY;
    }
}

void vca_env_set_attack(vca_env_t *env, float time_seconds)
{
    if (env == NULL)
    {
        return;
    }
    if (time_seconds < 0.0f)
    {
        time_seconds = 0.0f;
    }
    if (env->attack_time == time_seconds)
    {
        return;
    }
    env->attack_time = time_seconds;
    env->attack_coefficient = vca_env_coefficient(
        env->attack_time,
        env->sample_rate,
        1.0f - (1.0f / env->attack_target));
    if (env->stage == VCA_ENV_ATTACK)
    {
        vca_env_prepare_attack(env);
    }
}

void vca_env_set_decay(vca_env_t *env, float time_seconds)
{
    if (env == NULL)
    {
        return;
    }
    if (time_seconds < 0.0f)
    {
        time_seconds = 0.0f;
    }
    if (env->decay_time == time_seconds)
    {
        return;
    }
    env->decay_time = time_seconds;
    env->decay_coefficient = vca_env_coefficient(
        env->decay_time,
        env->sample_rate,
        expf(-1.0f));
    if (env->stage == VCA_ENV_DECAY)
    {
        vca_env_prepare_decay(env);
    }
}

void vca_env_set_sustain(vca_env_t *env, float sustain)
{
    if (env != NULL)
    {
        env->sustain = vca_env_clamp_level(sustain);
    }
}

void vca_env_set_release(vca_env_t *env, float time_seconds)
{
    if (env == NULL)
    {
        return;
    }
    if (time_seconds < 0.0f)
    {
        time_seconds = 0.0f;
    }
    if (env->release_time == time_seconds)
    {
        return;
    }
    env->release_time = time_seconds;
    if (env->stage == VCA_ENV_RELEASE)
    {
        vca_env_prepare_release(env);
    }
}

void vca_env_gate_on(vca_env_t *env)
{
    if (env != NULL)
    {
        env->gate = true;
    }
}

void vca_env_gate_off(vca_env_t *env)
{
    if ((env == NULL) || (env->stage == VCA_ENV_IDLE))
    {
        return;
    }
    env->gate = false;
    env->stage = VCA_ENV_RELEASE;
    vca_env_prepare_release(env);
}

void vca_env_retrigger(vca_env_t *env, bool hard_reset)
{
    if (env == NULL)
    {
        return;
    }
    if (hard_reset)
    {
        env->level = 0.0f;
    }
    env->stage = VCA_ENV_ATTACK;
    env->gate = true;
    vca_env_prepare_attack(env);
}

uint8_t vca_env_process_daisy(vca_env_t *env, float *out_gain)
{
    if ((env == NULL) || (out_gain == NULL))
    {
        return 0U;
    }
    switch (env->stage)
    {
        case VCA_ENV_ATTACK:
            env->level += env->attack_coefficient
                        * (env->attack_target - env->level);
            if ((env->level >= 1.0f) || (--env->samples_remaining == 0U))
            {
                env->level = 1.0f;
                env->stage = VCA_ENV_DECAY;
                vca_env_prepare_decay(env);
            }
            break;
        case VCA_ENV_DECAY:
            env->level += env->decay_coefficient
                        * (env->sustain - env->level);
            if (--env->samples_remaining == 0U)
            {
                env->level = env->sustain;
                env->stage = VCA_ENV_SUSTAIN;
            }
            break;
        case VCA_ENV_SUSTAIN:
            env->level = env->sustain;
            break;
        case VCA_ENV_RELEASE:
            env->level += env->release_coefficient
                        * (env->release_target - env->level);
            if ((env->level <= 0.0f) || (--env->samples_remaining == 0U))
            {
                env->level = 0.0f;
                env->stage = VCA_ENV_IDLE;
            }
            break;
        default:
            env->level = 0.0f;
            env->stage = VCA_ENV_IDLE;
            *out_gain = 0.0f;
            return 0U;
    }
    *out_gain = env->level;
    return 1U;
}

uint8_t vca_env_process_linear(vca_env_t *env, float *out_gain)
{
    if ((env == NULL) || (out_gain == NULL))
    {
        return 0U;
    }
    switch (env->stage)
    {
        case VCA_ENV_ATTACK:
            env->level += env->linear_increment;
            if (--env->samples_remaining == 0U)
            {
                env->level = 1.0f;
                env->stage = VCA_ENV_DECAY;
                vca_env_prepare_decay(env);
            }
            break;
        case VCA_ENV_DECAY:
            env->level += env->linear_increment;
            if (--env->samples_remaining == 0U)
            {
                env->level = env->sustain;
                env->stage = VCA_ENV_SUSTAIN;
            }
            break;
        case VCA_ENV_SUSTAIN:
            env->level = env->sustain;
            break;
        case VCA_ENV_RELEASE:
            env->level += env->linear_increment;
            if (--env->samples_remaining == 0U)
            {
                env->level = 0.0f;
                env->stage = VCA_ENV_IDLE;
            }
            break;
        default:
            env->level = 0.0f;
            env->stage = VCA_ENV_IDLE;
            *out_gain = 0.0f;
            return 0U;
    }
    *out_gain = env->level;
    return 1U;
}

vca_env_stage_t vca_env_stage(const vca_env_t *env)
{
    return (env != NULL) ? env->stage : VCA_ENV_IDLE;
}
