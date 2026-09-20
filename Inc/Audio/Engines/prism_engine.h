/**
 * @file prism_engine.h
 * @brief Minimal track-aware Prism runtime wrapper around the internal Braids engine.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float edit;
    float pitch_mod;
    float timbre;
    float modulation;
    float color;
    float note;
    float velocity;
    uint8_t active_note;
    uint8_t has_active_note;
    uint8_t gate;
    uint8_t trigger;
} brick6_braids_runtime_voice_t;

#define BRICK6_BRAIDS_MAX_INSTANCES 8U
#define BRICK6_BRAIDS_VOICE_INSTANCE_COUNT 16U
#include "Param/engine_model_catalog.h"

void brick6_braids_runtime_init(void);
void brick6_braids_runtime_reset_instance(uint8_t instance_id);

void brick6_braids_runtime_set_edit(uint8_t instance_id, float edit);
void brick6_braids_runtime_set_pitch_mod(uint8_t instance_id, float amount);
void brick6_braids_runtime_set_timbre(uint8_t instance_id, float timbre);
void brick6_braids_runtime_set_modulation(uint8_t instance_id, float modulation);
void brick6_braids_runtime_set_color(uint8_t instance_id, float color);
void brick6_braids_runtime_set_phase_reset(uint8_t instance_id, uint8_t enabled);
void brick6_braids_runtime_set_osc_edit(uint8_t instance_id, uint8_t osc, float edit);
void brick6_braids_runtime_set_osc_pitch_mod(uint8_t instance_id, uint8_t osc, float amount);
void brick6_braids_runtime_set_osc_timbre(uint8_t instance_id, uint8_t osc, float timbre);
void brick6_braids_runtime_set_osc_modulation(uint8_t instance_id, uint8_t osc, float modulation);
void brick6_braids_runtime_set_osc_color(uint8_t instance_id, uint8_t osc, float color);
uint8_t brick6_braids_runtime_get_osc_model(uint8_t instance_id, uint8_t osc);
void brick6_braids_runtime_set_volume(uint8_t instance_id, float volume);
void brick6_braids_runtime_set_balance(uint8_t instance_id, float balance);
void brick6_braids_runtime_set_tune(uint8_t instance_id, float semitones);
void brick6_braids_runtime_set_detune(uint8_t instance_id, float semitones);
void brick6_braids_runtime_set_drift(uint8_t instance_id, float amount);
void brick6_braids_runtime_set_vca_release_seconds(uint8_t instance_id, float release_s);

void brick6_braids_runtime_note_on(uint8_t instance_id, float note, float velocity);
void brick6_braids_runtime_initialize_held_note(uint8_t instance_id, float note, float velocity);
void brick6_braids_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_braids_runtime_all_notes_off(uint8_t instance_id);
void brick6_braids_runtime_clear_trigger(uint8_t instance_id);
uint8_t brick6_braids_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames);
void brick6_braids_runtime_sync_voice(uint8_t track_instance, uint8_t voice_instance);

const brick6_braids_runtime_voice_t *brick6_braids_runtime_get_voice(uint8_t instance_id);

#ifdef __cplusplus
}
#endif
