#ifndef AUDIO_MICRODEXED_SYNTH_H
#define AUDIO_MICRODEXED_SYNTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void microdexed_synth_init(float sample_rate, uint32_t block_size);
void microdexed_synth_set_enabled(uint8_t enabled);
uint8_t microdexed_synth_is_enabled(void);
void microdexed_synth_note_on(uint8_t midi_note, uint8_t velocity);
void microdexed_synth_note_off(uint8_t midi_note);
void microdexed_synth_all_notes_off(void);
void microdexed_synth_process_block(float *mono_out, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_MICRODEXED_SYNTH_H */
