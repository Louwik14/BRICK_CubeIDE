#ifndef FX_CLOUDS_WRAPPER_H
#define FX_CLOUDS_WRAPPER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fx_clouds_init(float sample_rate);
void fx_clouds_process_block(float* inL, float* inR, float* outL, float* outR, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif
