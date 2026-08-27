#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MD_DSP_SINE_LUT_BITS 10U
#define MD_DSP_SINE_LUT_SIZE (1U << MD_DSP_SINE_LUT_BITS)

typedef struct
{
    uint32_t phase;
    uint32_t increment;
} md_phase_t;

typedef struct
{
    float value;
    float coefficient;
} md_decay_env_t;

typedef struct
{
    uint32_t state;
} md_rng_t;

typedef struct
{
    float x1;
    float y1;
    float coefficient;
} md_hpf_t;

typedef struct
{
    float state;
    float coefficient;
} md_lpf_t;

typedef struct
{
    float previous_tail;
    float gain;
    float decrement;
} md_retrigger_fade_t;

uint32_t md_phase_increment_from_hz(float frequency_hz, float sample_rate);
void md_phase_set_frequency(md_phase_t *osc, float frequency_hz, float sample_rate);
void md_phase_reset(md_phase_t *osc, uint32_t phase);
float md_phase_sine_next(md_phase_t *osc);
float md_phase_square_next(md_phase_t *osc);

float md_decay_coefficient(float seconds, float sample_rate);
void md_decay_env_prepare(md_decay_env_t *env, float seconds, float sample_rate);
void md_decay_env_trigger(md_decay_env_t *env, float level);
float md_decay_env_process(md_decay_env_t *env);

void md_rng_seed(md_rng_t *rng, uint32_t seed);
uint32_t md_rng_next_u32(md_rng_t *rng);
float md_rng_next_bipolar(md_rng_t *rng);

void md_hpf_prepare(md_hpf_t *filter, float coefficient);
void md_hpf_reset(md_hpf_t *filter);
float md_hpf_process(md_hpf_t *filter, float input);
void md_lpf_prepare(md_lpf_t *filter, float coefficient);
void md_lpf_reset(md_lpf_t *filter);
float md_lpf_process(md_lpf_t *filter, float input);

float md_clip(float input, float drive);
float md_mix2(float a, float b, float balance);
void md_retrigger_fade_begin(md_retrigger_fade_t *fade, float previous_tail, uint16_t samples);
float md_retrigger_fade_process(md_retrigger_fade_t *fade, float fresh);

#ifdef __cplusplus
}
#endif
