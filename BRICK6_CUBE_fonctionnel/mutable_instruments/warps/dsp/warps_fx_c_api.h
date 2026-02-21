#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void warps_fx_engine_init(float sample_rate);
void warps_fx_engine_process(float* inL,
                             float* inR,
                             float* outL,
                             float* outR,
                             int size);
void warps_fx_engine_set_algorithm(float algo);
void warps_fx_engine_set_parameter(float param);
void warps_fx_engine_set_drive(float d1, float d2);

#ifdef __cplusplus
}
#endif
