#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float sample_rate;
    float cutoff_hz;
    float resonance;
    float drive;
    double stage[4];
    double delta[4];
    double tanh_stage[4];
    double g;
} fx_filter_ladder_moog_t;

void fx_filter_ladder_moog_init(fx_filter_ladder_moog_t *filter, float sample_rate);
void fx_filter_ladder_moog_reset(fx_filter_ladder_moog_t *filter);
void fx_filter_ladder_moog_set_sample_rate(fx_filter_ladder_moog_t *filter, float sample_rate);
void fx_filter_ladder_moog_set_cutoff(fx_filter_ladder_moog_t *filter, float cutoff_hz);
void fx_filter_ladder_moog_set_resonance(fx_filter_ladder_moog_t *filter, float resonance);
void fx_filter_ladder_moog_set_drive(fx_filter_ladder_moog_t *filter, float drive);
float fx_filter_ladder_moog_process_sample(fx_filter_ladder_moog_t *filter, float input);
void fx_filter_ladder_moog_process_block(fx_filter_ladder_moog_t *filter, float *samples, uint32_t frames);

#ifdef __cplusplus
}
#endif
