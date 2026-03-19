#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void juno_synth_init(float sample_rate, uint32_t block_size);
void juno_synth_set_enabled(uint8_t enabled);
uint8_t juno_synth_is_enabled(void);
void juno_synth_set_test_mode(uint8_t enabled);
void juno_synth_process_block(float *mono_out, uint32_t frames);

#ifdef __cplusplus
}
#endif
