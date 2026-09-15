#ifndef BRICK6_ACID_ENGINE_H
#define BRICK6_ACID_ENGINE_H
#include <stdint.h>
#define BRICK6_ACID_INSTANCE_COUNT 16U
void brick6_acid_runtime_init(void);
void brick6_acid_runtime_reset_instance(uint8_t id);
void brick6_acid_runtime_restart_voice(uint8_t id);
void brick6_acid_runtime_sync_voice(uint8_t source, uint8_t destination);
void brick6_acid_runtime_note_on(uint8_t id, uint8_t note, uint8_t velocity);
void brick6_acid_runtime_initialize_held_note(uint8_t id, uint8_t note, uint8_t velocity);
void brick6_acid_runtime_note_off(uint8_t id, uint8_t note);
void brick6_acid_runtime_all_notes_off(uint8_t id);
uint8_t brick6_acid_runtime_render_instance(uint8_t id, float *out, uint32_t frames);
void brick6_acid_runtime_set_wave(uint8_t id, uint8_t square);
void brick6_acid_runtime_set_tune(uint8_t id, float semitones);
void brick6_acid_runtime_set_cut(uint8_t id, float normalized);
void brick6_acid_runtime_set_res(uint8_t id, float normalized);
void brick6_acid_runtime_set_env_mod(uint8_t id, float normalized);
void brick6_acid_runtime_set_decay(uint8_t id, float normalized);
void brick6_acid_runtime_set_accent(uint8_t id, float normalized);
void brick6_acid_runtime_set_slide(uint8_t id, uint8_t enabled);
void brick6_acid_runtime_set_cutoff_rate(uint8_t id, uint8_t selection);
#endif
