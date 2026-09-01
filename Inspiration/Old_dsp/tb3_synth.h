#pragma once

#include <stdint.h>

#include "Param/param_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

void tb3_synth_init(float sample_rate);
uint8_t tb3_synth_instance_count(void);

void tb3_synth_note_on_for_instance(uint8_t instance_id, uint8_t midi_note, uint8_t velocity);
void tb3_synth_note_off_for_instance(uint8_t instance_id, uint8_t midi_note);
void tb3_synth_all_notes_off_for_instance(uint8_t instance_id);
void tb3_synth_all_notes_off_all(void);

void tb3_synth_set_param_for_instance(uint8_t instance_id, param_id_t param_id, float value);
void tb3_synth_process_block_for_instance(uint8_t instance_id, float *mono_out, uint32_t frames);

#ifdef __cplusplus
}
#endif
