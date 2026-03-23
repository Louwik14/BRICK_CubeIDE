#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t counter_valid;
    uint32_t frames_last;
    uint32_t osc_last_cycles;
    uint32_t osc_avg_cycles;
    uint32_t block_last_cycles;
    uint32_t block_avg_cycles;
    uint32_t sample_count;
} monob_synth_profile_t;

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
void monob_synth_set_osc_wave(uint8_t osc_index, uint8_t wave);
void monob_synth_set_osc_range(uint8_t osc_index, int8_t octave);
void monob_synth_set_sub_octave(int8_t octave);
void monob_synth_set_osc_detune(uint8_t osc_index, float detune_cents);
void monob_synth_set_osc_mix(uint8_t osc_index, float mix);
void monob_synth_set_sub_mix(float mix);
void monob_synth_get_profile(monob_synth_profile_t *out_profile);
void monob_synth_reset_profile(void);

#ifdef __cplusplus
}
#endif
