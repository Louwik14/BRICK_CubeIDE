/**
 * @file brick6_opal_runtime.h
 * @brief Minimal Opal runtime facade backed by the Plaits 6-op engine.
 *
 * Role:
 * - Reserve a bounded physical runtime pool for Opal.
 * - Keep runtime state local, explicit, and allocation-free.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float patch;
    float index;
    float time;
    float note;
    float velocity;
    uint8_t active_note;
    uint8_t has_active_note;
    uint8_t gate;
    uint8_t trigger;
} brick6_opal_runtime_voice_t;

#define BRICK6_PLAITS_MAX_INSTANCES 1U

void brick6_opal_runtime_init(void);
void brick6_opal_runtime_reset_instance(uint8_t instance_id);

void brick6_opal_runtime_set_harmonics(uint8_t instance_id, float patch);
void brick6_opal_runtime_set_timbre(uint8_t instance_id, float index);
void brick6_opal_runtime_set_morph(uint8_t instance_id, float time);

void brick6_opal_runtime_note_on(uint8_t instance_id, float note, float velocity);
void brick6_opal_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_opal_runtime_all_notes_off(uint8_t instance_id);
void brick6_opal_runtime_clear_trigger(uint8_t instance_id);
void brick6_opal_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames);

const brick6_opal_runtime_voice_t *brick6_opal_runtime_get_voice(uint8_t instance_id);

#ifdef __cplusplus
}
#endif
