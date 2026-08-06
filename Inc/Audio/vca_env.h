#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    VCA_ENV_TYPE_DAISY = 0,
    VCA_ENV_TYPE_LINEAR,
} vca_env_type_t;

typedef enum
{
    VCA_ENV_IDLE = 0,
    VCA_ENV_ATTACK,
    VCA_ENV_DECAY,
    VCA_ENV_SUSTAIN,
    VCA_ENV_RELEASE,
} vca_env_stage_t;

typedef struct
{
    vca_env_type_t type;
    vca_env_stage_t stage;
    float sample_rate;
    float attack_time;
    float decay_time;
    float release_time;
    float level;
    float sustain;
    float attack_coefficient;
    float decay_coefficient;
    float release_coefficient;
    float attack_target;
    float release_target;
    float linear_increment;
    uint32_t samples_remaining;
    bool sustain_transition_active;
    bool gate;
} vca_env_t;

void vca_env_init(vca_env_t *env, float sample_rate);
void vca_env_reset(vca_env_t *env);
void vca_env_set_type(vca_env_t *env, vca_env_type_t type);
void vca_env_set_attack(vca_env_t *env, float time_seconds);
void vca_env_set_decay(vca_env_t *env, float time_seconds);
void vca_env_set_sustain(vca_env_t *env, float sustain);
void vca_env_set_release(vca_env_t *env, float time_seconds);
void vca_env_gate_on(vca_env_t *env);
void vca_env_gate_off(vca_env_t *env);
void vca_env_retrigger(vca_env_t *env, bool hard_reset);
uint8_t vca_env_process_daisy(vca_env_t *env, float *out_gain);
uint8_t vca_env_process_linear(vca_env_t *env, float *out_gain);
vca_env_stage_t vca_env_stage(const vca_env_t *env);
