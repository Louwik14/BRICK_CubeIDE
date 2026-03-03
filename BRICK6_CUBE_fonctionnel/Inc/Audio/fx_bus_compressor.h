#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fx_bus_compressor_init(float sample_rate, uint32_t block_size);
void fx_bus_compressor_set_threshold_db(float threshold_db);
void fx_bus_compressor_set_ratio(float ratio);
void fx_bus_compressor_set_attack_index(uint8_t attack_index);
void fx_bus_compressor_set_release_index(uint8_t release_index);
void fx_bus_compressor_set_makeup(float db);
void fx_bus_compressor_set_mix(float mix);
void fx_bus_compressor_set_hpf(float hz);
void fx_bus_compressor_process_stereo(float *left,
                                      float *right,
                                      uint32_t frames);

#ifdef __cplusplus
}
#endif
