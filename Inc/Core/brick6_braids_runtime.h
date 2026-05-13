/**
 * @file brick6_braids_runtime.h
 * @brief Minimal track-aware Braids runtime wrapper.
 */

#pragma once

#include <stdint.h>
#include "Seq/seq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float edit;
    float fine;
    float coarse;
    float fm;
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

#define BRICK6_BRAIDS_MAX_INSTANCES SEQ_TRACK_COUNT

void brick6_braids_runtime_init(void);
void brick6_braids_runtime_reset_instance(uint8_t instance_id);

void brick6_braids_runtime_set_edit(uint8_t instance_id, float edit);
void brick6_braids_runtime_set_fine(uint8_t instance_id, float fine);
void brick6_braids_runtime_set_coarse(uint8_t instance_id, float coarse);
void brick6_braids_runtime_set_fm(uint8_t instance_id, float fm);
void brick6_braids_runtime_set_timbre(uint8_t instance_id, float timbre);
void brick6_braids_runtime_set_modulation(uint8_t instance_id, float modulation);
void brick6_braids_runtime_set_color(uint8_t instance_id, float color);
void brick6_braids_runtime_set_phase_reset(uint8_t instance_id, uint8_t enabled);
void brick6_braids_runtime_set_vca_release_seconds(uint8_t instance_id, float release_s);

void brick6_braids_runtime_note_on(uint8_t instance_id, float note, float velocity);
void brick6_braids_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_braids_runtime_all_notes_off(uint8_t instance_id);
void brick6_braids_runtime_clear_trigger(uint8_t instance_id);
void brick6_braids_runtime_render_instance(uint8_t instance_id, float *out_mono, uint32_t frames);

const brick6_braids_runtime_voice_t *brick6_braids_runtime_get_voice(uint8_t instance_id);

#ifdef __cplusplus
}
#endif
