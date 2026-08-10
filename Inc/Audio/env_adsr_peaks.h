#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    ENV_ADSR_PEAKS_STAGE_IDLE = 0,
    ENV_ADSR_PEAKS_STAGE_ATTACK,
    ENV_ADSR_PEAKS_STAGE_DECAY,
    ENV_ADSR_PEAKS_STAGE_SUSTAIN,
    ENV_ADSR_PEAKS_STAGE_RELEASE
} env_adsr_peaks_stage_t;

typedef struct
{
    uint16_t attack;
    uint16_t decay;
    uint16_t sustain;
    uint16_t release;

    uint32_t attack_increment;
    uint32_t decay_increment;
    uint32_t release_increment;

    uint32_t max_segment_samples;

    uint32_t phase;
    uint32_t phase_increment;

    int16_t start_value;
    int16_t target_value;
    int16_t value;

    float release_level;
    float release_coefficient;

    bool gate_high;
    bool hard_reset;
    uint8_t stage;
} env_adsr_peaks_t;

void env_adsr_peaks_init(env_adsr_peaks_t *env, float sample_rate);
void env_adsr_peaks_reset(env_adsr_peaks_t *env);

void env_adsr_peaks_set_attack(env_adsr_peaks_t *env, uint16_t attack);
void env_adsr_peaks_set_decay(env_adsr_peaks_t *env, uint16_t decay);
void env_adsr_peaks_set_sustain(env_adsr_peaks_t *env, uint16_t sustain);
void env_adsr_peaks_set_release(env_adsr_peaks_t *env, uint16_t release);

void env_adsr_peaks_gate_on(env_adsr_peaks_t *env);
void env_adsr_peaks_gate_off(env_adsr_peaks_t *env);
void env_adsr_peaks_retrigger(env_adsr_peaks_t *env, bool hard_reset);

int16_t env_adsr_peaks_process_step(env_adsr_peaks_t *env);
int16_t env_adsr_peaks_process_advance(env_adsr_peaks_t *env,
                                       uint32_t steps,
                                       int16_t *first_value);
uint32_t env_adsr_peaks_process_vca_block(env_adsr_peaks_t *env,
                                          float *out_gain,
                                          uint32_t frames);
int16_t env_adsr_peaks_value(const env_adsr_peaks_t *env);
env_adsr_peaks_stage_t env_adsr_peaks_stage(const env_adsr_peaks_t *env);
