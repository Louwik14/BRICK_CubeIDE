#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void monob_moog_ladder_init(float sample_rate);
void monob_moog_ladder_reset(void);
void monob_moog_ladder_set_cutoff(float cutoff_hz);
void monob_moog_ladder_set_resonance(float resonance);
float monob_moog_ladder_process_sample(float input);

#ifdef __cplusplus
}
#endif
