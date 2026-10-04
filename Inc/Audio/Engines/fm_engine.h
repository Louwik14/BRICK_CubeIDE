#pragma once

#include <stdint.h>
#include "ControlRT/fm_dsp_projection.h"
#include "Param/param_ids.h"
#include "Param/engine_model_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_FM_VOICE_COUNT 16U
#define BRICK6_FM_RENDER_BLOCK 64U

void brick6_fm_runtime_init(void);
void brick6_fm_runtime_reset_instance(uint8_t instance_id);
void brick6_fm_runtime_all_notes_off(uint8_t instance_id);
void brick6_fm_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity);
void brick6_fm_runtime_initialize_held_note(uint8_t instance_id, uint8_t note, uint8_t velocity);
void brick6_fm_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_fm_runtime_set_ratio(uint8_t instance_id, float value);
void brick6_fm_runtime_set_algorithm(uint8_t instance_id, uint8_t algorithm);
void brick6_fm_runtime_set_feedback(uint8_t instance_id, uint8_t feedback);
void brick6_fm_runtime_set_sync(uint8_t instance_id, uint8_t enabled);
void brick6_fm_runtime_set_bright(uint8_t instance_id, float value);
void brick6_fm_runtime_set_body(uint8_t instance_id, float value);
void brick6_fm_runtime_set_detail(uint8_t instance_id, float value);
void brick6_fm_runtime_set_metal(uint8_t instance_id, float value);
void brick6_fm_runtime_set_env(uint8_t instance_id,
                               float attack,
                               float decay,
                               float sustain,
                               float release);
void brick6_fm_runtime_set_play(uint8_t instance_id,
                                float velocity,
                                float key_scaling,
                                float pitch_env,
                                float pitch_time);
void brick6_fm_runtime_set_operator(uint8_t instance_id,
                                    uint8_t operator_id,
                                    brick6_fm_operator_param_t param,
                                    float value);
void brick6_fm_runtime_set_base_voice(uint8_t instance_id,
                                      const track_tone_fm_base_voice_t *base);
/* AUDIO-private sample-boundary commit. Setters only update target state and
 * dirty masks; this performs each bounded derived-state rebuild once. */
void brick6_fm_runtime_finalize_pending(void);
uint8_t brick6_fm_runtime_get_base_voice(uint8_t instance_id,
                                         track_tone_fm_base_voice_t *out_base);
uint8_t brick6_fm_runtime_get_macros(uint8_t instance_id,
                                     track_tone_fm_macros_t *out_macros);
void brick6_fm_runtime_sync_voice(uint8_t source_instance_id, uint8_t destination_instance_id);
void brick6_fm_runtime_sync_voice_if_needed(uint8_t source_instance_id,
                                            uint8_t destination_instance_id);
uint8_t brick6_fm_runtime_render_instance(uint8_t instance_id,
                                          float *out_mono,
                                          uint32_t frames);

/* Dedicated Patch Preview voice. It is not indexed by, registered with, or
 * visible to synth_polyphony. */
void brick6_fm_preview_reset(void);
uint8_t brick6_fm_preview_prepare(const track_tone_fm_base_voice_t *base,
                                  const track_tone_fm_macros_t *macros);
void brick6_fm_preview_note_on(uint8_t note, uint8_t velocity);
void brick6_fm_preview_note_off(uint8_t note);
uint8_t brick6_fm_preview_render(float *out_mono, uint32_t frames);
uint8_t brick6_fm_preview_apply_param(param_id_t parameter, float value);

#ifdef __cplusplus
}
#endif
