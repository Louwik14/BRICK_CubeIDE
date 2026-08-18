#pragma once
#include <stdint.h>
typedef enum { FX_DELUGE_MORPH_LP=0, FX_DELUGE_MORPH_LP_BP, FX_DELUGE_MORPH_BP, FX_DELUGE_MORPH_BP_HP, FX_DELUGE_MORPH_HP } fx_deluge_morph_plan_t;
typedef struct { float low; float band; } fx_deluge_svf_channel_t;
typedef struct {
    fx_deluge_svf_channel_t channel[2];
    float sample_rate, frequency, damping, input_gain, morph_a, morph_b;
    uint8_t morph_plan;
} fx_deluge_filter_t;
void fx_deluge_filter_init(fx_deluge_filter_t *filter, float sample_rate);
void fx_deluge_filter_reset(fx_deluge_filter_t *filter);
void fx_deluge_filter_configure(fx_deluge_filter_t *filter, float morph, float cutoff_hz, float resonance);
void fx_deluge_filter_process(fx_deluge_filter_t *filter, float *left, float *right, uint32_t frames);
void fx_deluge_filter_process_mono(fx_deluge_filter_t *filter, float *samples, uint32_t frames);
