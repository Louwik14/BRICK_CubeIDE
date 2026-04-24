/**
 * @file brick6_plaits_runtime.h
 * @brief Minimal Plaits runtime wrapper facade.
 *
 * Role:
 * - Reserve a bounded physical runtime pool for Plaits v1.
 * - Keep runtime state local, explicit, and allocation-free.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float model;
    float coarse_frequency;
    float harmonics;
    float timbre;
    float morph;
    float lpg_response;
    float decay;
    float frequency_range;
    float note;
    float velocity;
    uint8_t active_note;
    uint8_t has_active_note;
    uint8_t gate;
    uint8_t trigger;
} brick6_plaits_runtime_voice_t;

#define BRICK6_PLAITS_MAX_INSTANCES 1U

void brick6_plaits_runtime_init(void);
void brick6_plaits_runtime_reset_instance(uint8_t instance_id);

void brick6_plaits_runtime_set_model(uint8_t instance_id, float model);
void brick6_plaits_runtime_set_coarse_frequency(uint8_t instance_id, float coarse_frequency);
void brick6_plaits_runtime_set_harmonics(uint8_t instance_id, float harmonics);
void brick6_plaits_runtime_set_timbre(uint8_t instance_id, float timbre);
void brick6_plaits_runtime_set_morph(uint8_t instance_id, float morph);
void brick6_plaits_runtime_set_lpg_response(uint8_t instance_id, float lpg_response);
void brick6_plaits_runtime_set_decay(uint8_t instance_id, float decay);
void brick6_plaits_runtime_set_frequency_range(uint8_t instance_id, float frequency_range);

void brick6_plaits_runtime_note_on(uint8_t instance_id, float note, float velocity);
void brick6_plaits_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_plaits_runtime_all_notes_off(uint8_t instance_id);
void brick6_plaits_runtime_clear_trigger(uint8_t instance_id);
void brick6_plaits_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames);

const brick6_plaits_runtime_voice_t *brick6_plaits_runtime_get_voice(uint8_t instance_id);

#ifdef __cplusplus
}
#endif
