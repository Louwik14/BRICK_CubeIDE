#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void monob_osc_bank_init(float sample_rate);
void monob_osc_bank_init_for_instance(uint8_t instance_id, float sample_rate);
void monob_osc_bank_reset(void);
void monob_osc_bank_reset_for_instance(uint8_t instance_id);
void monob_osc_bank_note_on(void);
void monob_osc_bank_note_on_for_instance(uint8_t instance_id);
void monob_osc_bank_set_wave(uint8_t osc_index, uint8_t wave);
void monob_osc_bank_set_wave_for_instance(uint8_t instance_id, uint8_t osc_index, uint8_t wave);
void monob_osc_bank_set_octave(uint8_t osc_index, int8_t octave);
void monob_osc_bank_set_octave_for_instance(uint8_t instance_id, uint8_t osc_index, int8_t octave);
void monob_osc_bank_set_detune(uint8_t osc_index, float detune_cents);
void monob_osc_bank_set_detune_for_instance(uint8_t instance_id, uint8_t osc_index, float detune_cents);
void monob_osc_bank_set_mix(uint8_t osc_index, float mix);
void monob_osc_bank_set_mix_for_instance(uint8_t instance_id, uint8_t osc_index, float mix);
float monob_osc_bank_process(float base_frequency_hz);
float monob_osc_bank_process_for_instance(uint8_t instance_id, float base_frequency_hz);

#ifdef __cplusplus
}
#endif
