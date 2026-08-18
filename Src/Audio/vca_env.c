#include "Audio/vca_env.h"
#include <math.h>
#include <stddef.h>

static uint32_t time_samples(const vca_env_t *env, float seconds)
{
    if(seconds <= 0.0f) return 1U;
    const float value = seconds * env->sample_rate;
    return (value < 1.0f) ? 1U : (uint32_t)value;
}
static float coefficient(float seconds, float sample_rate, float ratio)
{
    if((seconds <= 0.0f) || (sample_rate <= 0.0f) || (ratio <= 0.0f)) return 1.0f;
    return 1.0f - expf(logf(ratio) / (seconds * sample_rate));
}
static float clamp_level(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }
static void prepare(vca_env_t *env)
{
    env->sustain_transition_active = false;
    if(env->stage == VCA_ENV_ATTACK)
        env->samples_remaining = time_samples(env, env->attack_time);
    else if(env->stage == VCA_ENV_DECAY)
        env->samples_remaining = time_samples(env, env->decay_time);
    else if(env->stage == VCA_ENV_RELEASE)
    {
        env->samples_remaining = time_samples(env, env->release_time);
        env->release_coefficient = (env->level <= 0.0f) ? 1.0f
            : coefficient(env->release_time, env->sample_rate,
                          (-env->release_target) / (env->level - env->release_target));
    }
}
void vca_env_init(vca_env_t *env, float sample_rate)
{
    if(env == NULL) return;
    env->sample_rate = sample_rate;
    env->attack_time = env->decay_time = env->release_time = 0.001f;
    env->sustain = 1.0f;
    env->attack_target = 1.01f;
    env->release_target = -0.01f;
    env->attack_coefficient = coefficient(env->attack_time, sample_rate, 1.0f - 1.0f / env->attack_target);
    env->decay_coefficient = coefficient(env->decay_time, sample_rate, expf(-1.0f));
    env->release_coefficient = 1.0f;
    vca_env_reset(env);
}
void vca_env_reset(vca_env_t *env)
{
    if(env == NULL) return;
    env->stage = VCA_ENV_IDLE; env->level = 0.0f; env->samples_remaining = 0U;
    env->sustain_transition_active = false; env->gate = false;
}
void vca_env_set_attack(vca_env_t *env, float seconds)
{
    if(env == NULL) return;
    env->attack_time = seconds < 0.0f ? 0.0f : seconds;
    env->attack_coefficient = coefficient(env->attack_time, env->sample_rate, 1.0f - 1.0f / env->attack_target);
    if(env->stage == VCA_ENV_ATTACK) prepare(env);
}
void vca_env_set_decay(vca_env_t *env, float seconds)
{
    if(env == NULL) return;
    env->decay_time = seconds < 0.0f ? 0.0f : seconds;
    env->decay_coefficient = coefficient(env->decay_time, env->sample_rate, expf(-1.0f));
    if(env->stage == VCA_ENV_DECAY) prepare(env);
}
void vca_env_set_sustain(vca_env_t *env, float sustain)
{
    if(env != NULL) env->sustain = clamp_level(sustain);
}
void vca_env_set_release(vca_env_t *env, float seconds)
{
    if(env == NULL) return;
    env->release_time = seconds < 0.0f ? 0.0f : seconds;
    if(env->stage == VCA_ENV_RELEASE) prepare(env);
}
void vca_env_gate_on(vca_env_t *env) { if(env != NULL) env->gate = true; }
void vca_env_gate_off(vca_env_t *env)
{
    if((env == NULL) || (env->stage == VCA_ENV_IDLE)) return;
    env->gate = false; env->stage = VCA_ENV_RELEASE; prepare(env);
}
void vca_env_retrigger(vca_env_t *env, bool hard_reset)
{
    if(env == NULL) return;
    if(hard_reset) env->level = 0.0f;
    env->stage = VCA_ENV_ATTACK; env->gate = true; prepare(env);
}
uint8_t vca_env_process(vca_env_t *env, float *out)
{
    if((env == NULL) || (out == NULL)) return 0U;
    switch(env->stage)
    {
        case VCA_ENV_ATTACK:
            env->level += env->attack_coefficient * (env->attack_target - env->level);
            if((env->level >= 1.0f) || (--env->samples_remaining == 0U))
            { env->level = 1.0f; env->stage = VCA_ENV_DECAY; prepare(env); }
            break;
        case VCA_ENV_DECAY:
            env->level += env->decay_coefficient * (env->sustain - env->level);
            if(--env->samples_remaining == 0U) { env->level = env->sustain; env->stage = VCA_ENV_SUSTAIN; }
            break;
        case VCA_ENV_SUSTAIN: env->level = env->sustain; break;
        case VCA_ENV_RELEASE:
            env->level += env->release_coefficient * (env->release_target - env->level);
            if((env->level <= 0.0f) || (--env->samples_remaining == 0U))
            { env->level = 0.0f; env->stage = VCA_ENV_IDLE; }
            break;
        default: env->level = 0.0f; env->stage = VCA_ENV_IDLE; *out = 0.0f; return 0U;
    }
    *out = env->level; return 1U;
}
uint32_t vca_env_process_block(vca_env_t *env, float *out, uint32_t frames)
{
    uint32_t produced = 0U;
    while(produced < frames)
    {
        if(vca_env_process(env, &out[produced]) == 0U) break;
        ++produced;
    }
    return produced;
}
vca_env_stage_t vca_env_stage(const vca_env_t *env) { return env != NULL ? env->stage : VCA_ENV_IDLE; }
