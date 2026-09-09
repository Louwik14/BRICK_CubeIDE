#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_TB303_INSTANCE_COUNT 16U

void brick6_tb303_runtime_init(void);
void brick6_tb303_runtime_reset_instance(uint8_t instance_id);
void brick6_tb303_runtime_note_on(uint8_t instance_id, uint8_t note,
                                  uint8_t velocity);
void brick6_tb303_runtime_initialize_held_note(uint8_t instance_id,
                                               uint8_t note,
                                               uint8_t velocity);
void brick6_tb303_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_tb303_runtime_all_notes_off(uint8_t instance_id);
void brick6_tb303_runtime_restart_voice(uint8_t instance_id);
void brick6_tb303_runtime_sync_voice(uint8_t source_instance_id,
                                     uint8_t destination_instance_id);
uint8_t brick6_tb303_runtime_render_instance(uint8_t instance_id,
                                             float *out_mono,
                                             uint32_t frames);

void brick6_tb303_runtime_set_wave(uint8_t instance_id, uint8_t square);
void brick6_tb303_runtime_set_tune(uint8_t instance_id, float semitones);
void brick6_tb303_runtime_set_cut(uint8_t instance_id, float normalized);
void brick6_tb303_runtime_set_res(uint8_t instance_id, float normalized);
void brick6_tb303_runtime_set_env_mod(uint8_t instance_id, float normalized);
void brick6_tb303_runtime_set_decay(uint8_t instance_id, float normalized);
void brick6_tb303_runtime_set_accent(uint8_t instance_id, float normalized);
void brick6_tb303_runtime_set_slide(uint8_t instance_id, uint8_t enabled);
void brick6_tb303_runtime_set_vcf_rate(uint8_t instance_id, uint8_t divided_by_four);

#ifdef __cplusplus
}
#endif
