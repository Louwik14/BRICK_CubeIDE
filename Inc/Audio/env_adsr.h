#pragma once

#include "env_adsr_peaks.h"

typedef env_adsr_peaks_t env_adsr_t;

typedef struct
{
    env_adsr_t filter_env;
    env_adsr_t volume_env;
} env_adsr_bank_t;

void env_adsr_init(env_adsr_t *env, float sample_rate);
void env_adsr_reset(env_adsr_t *env);
void env_adsr_set_attack(env_adsr_t *env, uint16_t attack);
void env_adsr_set_decay(env_adsr_t *env, uint16_t decay);
void env_adsr_set_sustain(env_adsr_t *env, uint16_t sustain);
void env_adsr_set_release(env_adsr_t *env, uint16_t release);
void env_adsr_gate_on(env_adsr_t *env);
void env_adsr_gate_off(env_adsr_t *env);
void env_adsr_retrigger(env_adsr_t *env, bool hard_reset);
int16_t env_adsr_process_step(env_adsr_t *env);
int16_t env_adsr_process_advance(env_adsr_t *env,
                                 uint32_t steps,
                                 int16_t *first_value);
int16_t env_adsr_value(const env_adsr_t *env);
env_adsr_peaks_stage_t env_adsr_stage(const env_adsr_t *env);

void env_adsr_bank_init(env_adsr_bank_t *bank, float sample_rate);
void env_adsr_bank_reset(env_adsr_bank_t *bank);
