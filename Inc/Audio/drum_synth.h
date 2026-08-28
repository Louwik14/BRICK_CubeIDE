#pragma once

#include <stdint.h>

#include "Audio/drum_model_ids.h"
#include "Param/param_store.h"

#ifdef __cplusplus
extern "C" {
#endif

void drum_synth_init(float sample_rate);
uint8_t drum_synth_set_model_for_instance(uint8_t instance_id, drum_model_id_t model_type);
drum_model_id_t drum_synth_get_model_for_instance(uint8_t instance_id);
uint8_t drum_synth_get_md_model_for_instance(uint8_t instance_id);

void drum_synth_note_on_for_instance(uint8_t instance_id, uint8_t midi_note, uint8_t velocity);
void drum_synth_note_off_for_instance(uint8_t instance_id, uint8_t midi_note);
void drum_synth_all_notes_off_for_instance(uint8_t instance_id);

void drum_synth_process_block_for_instance(uint8_t instance_id, float *mono_out, uint32_t frames);
uint8_t drum_synth_set_param_for_instance(uint8_t instance_id, param_id_t param, float value);

void drum_synth_all_notes_off_all(void);

#ifdef __cplusplus
}
#endif
