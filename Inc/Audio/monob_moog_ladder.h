#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void monob_moog_ladder_init(float sample_rate);
void monob_moog_ladder_init_for_instance(uint8_t instance_id, float sample_rate);
void monob_moog_ladder_reset(void);
void monob_moog_ladder_reset_for_instance(uint8_t instance_id);
void monob_moog_ladder_set_cutoff(float cutoff_hz);
void monob_moog_ladder_set_cutoff_for_instance(uint8_t instance_id, float cutoff_hz);
void monob_moog_ladder_set_resonance(float resonance);
void monob_moog_ladder_set_resonance_for_instance(uint8_t instance_id, float resonance);
float monob_moog_ladder_process_sample(float input);
float monob_moog_ladder_process_sample_for_instance(uint8_t instance_id, float input);

#ifdef __cplusplus
}
#endif
