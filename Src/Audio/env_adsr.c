#include "env_adsr.h"

void env_adsr_init(env_adsr_t *env, float sample_rate)
{
    env_adsr_peaks_init(env, sample_rate);
}

void env_adsr_reset(env_adsr_t *env)
{
    env_adsr_peaks_reset(env);
}

void env_adsr_set_attack(env_adsr_t *env, uint16_t attack)
{
    env_adsr_peaks_set_attack(env, attack);
}

void env_adsr_set_decay(env_adsr_t *env, uint16_t decay)
{
    env_adsr_peaks_set_decay(env, decay);
}

void env_adsr_set_sustain(env_adsr_t *env, uint16_t sustain)
{
    env_adsr_peaks_set_sustain(env, sustain);
}

void env_adsr_set_release(env_adsr_t *env, uint16_t release)
{
    env_adsr_peaks_set_release(env, release);
}

void env_adsr_gate_on(env_adsr_t *env)
{
    env_adsr_peaks_gate_on(env);
}

void env_adsr_gate_off(env_adsr_t *env)
{
    env_adsr_peaks_gate_off(env);
}

void env_adsr_retrigger(env_adsr_t *env, bool hard_reset)
{
    env_adsr_peaks_retrigger(env, hard_reset);
}

int16_t env_adsr_process_step(env_adsr_t *env)
{
    return env_adsr_peaks_process_step(env);
}

uint8_t env_adsr_process_vca_sample(env_adsr_t *env, float *out_gain)
{
    const int16_t value = env_adsr_process_step(env);
    if (out_gain != 0)
    {
        *out_gain = (float)value * (1.0f / 32767.0f);
    }
    return (env_adsr_stage(env) != ENV_ADSR_PEAKS_STAGE_IDLE) ? 1U : 0U;
}

int16_t env_adsr_process_advance(env_adsr_t *env,
                                 uint32_t steps,
                                 int16_t *first_value)
{
    return env_adsr_peaks_process_advance(env, steps, first_value);
}

int16_t env_adsr_value(const env_adsr_t *env)
{
    return env_adsr_peaks_value(env);
}

env_adsr_peaks_stage_t env_adsr_stage(const env_adsr_t *env)
{
    return env_adsr_peaks_stage(env);
}

void env_adsr_bank_init(env_adsr_bank_t *bank, float sample_rate)
{
    env_adsr_init(&bank->filter_env, sample_rate);
    env_adsr_init(&bank->volume_env, sample_rate);
}

void env_adsr_bank_reset(env_adsr_bank_t *bank)
{
    env_adsr_reset(&bank->filter_env);
    env_adsr_reset(&bank->volume_env);
}
