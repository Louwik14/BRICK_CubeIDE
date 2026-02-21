#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float algorithm;
    float parameter;
    float drive1;
    float drive2;
} warps_params_t;

void fx_warps_init(float sample_rate);
void fx_warps_process(float* inL, float* inR, float* outL, float* outR, int size);

void fx_warps_set_algorithm(float v);
void fx_warps_set_parameter(float v);
void fx_warps_set_drive(float d1, float d2);
void fx_warps_set_drywet(float v);

void fx_warps_enable(int enabled);

#ifdef __cplusplus
}
#endif
