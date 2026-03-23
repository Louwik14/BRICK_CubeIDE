#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void monob_synth_init(float sample_rate);
void monob_synth_note_on(uint8_t midi_note, uint8_t velocity);
void monob_synth_note_off(uint8_t midi_note);
void monob_synth_all_notes_off(void);
void monob_synth_process_block(float *mono_out, uint32_t frames);

void monob_synth_set_filter_type(uint8_t enabled);
void monob_synth_set_filter_cutoff(float cutoff_hz);
void monob_synth_set_filter_resonance(float resonance);
void monob_synth_set_filter_eg_amount(float eg_amount);
void monob_synth_set_filter_attack(float attack_s);
void monob_synth_set_filter_decay(float decay_s);
void monob_synth_set_filter_sustain(float sustain);
void monob_synth_set_filter_release(float release_s);

#ifdef __cplusplus
}
#endif
