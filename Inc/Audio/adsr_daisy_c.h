#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float sample_rate;
    float sus_level;
    float x;
    float attack_shape;
    float attack_target;
    float attack_time;
    float decay_time;
    float release_time;
    float attack_d0;
    float decay_d0;
    float release_d0;
    uint8_t mode;
    uint8_t gate;
} adsr_daisy_c_t;

void adsr_daisy_c_init(adsr_daisy_c_t *env, float sample_rate, uint32_t block_size);
void adsr_daisy_c_reset(adsr_daisy_c_t *env);
void adsr_daisy_c_set_attack(adsr_daisy_c_t *env, float time_s);
void adsr_daisy_c_set_decay(adsr_daisy_c_t *env, float time_s);
void adsr_daisy_c_set_sustain(adsr_daisy_c_t *env, float sustain);
void adsr_daisy_c_set_release(adsr_daisy_c_t *env, float time_s);
void adsr_daisy_c_retrigger(adsr_daisy_c_t *env, uint8_t hard);
float adsr_daisy_c_process(adsr_daisy_c_t *env, uint8_t gate);
uint8_t adsr_daisy_c_is_running(const adsr_daisy_c_t *env);
uint8_t adsr_daisy_c_is_sustaining(const adsr_daisy_c_t *env);
float adsr_daisy_c_current_level(const adsr_daisy_c_t *env);

#ifdef __cplusplus
}
#endif
