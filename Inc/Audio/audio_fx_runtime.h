#ifndef AUDIO_FX_RUNTIME_H
#define AUDIO_FX_RUNTIME_H

#include <stdint.h>

#include "Track/entity_types.h"
#include "Param/param_ids.h"
#include "Param/engine_model_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef const void *audio_fx_sample_plan_handle_t;

void audio_fx_runtime_init(void);
uint8_t audio_fx_runtime_is_param(param_id_t id);
uint8_t audio_fx_runtime_param_slot(param_id_t id, audio_fx_slot_t *out_slot);
uint8_t audio_fx_runtime_is_group_level_param(param_id_t id);
uint8_t audio_fx_runtime_get_model(brick_entity_id_t entity_id,
                                   audio_fx_slot_t slot);
uint8_t audio_fx_runtime_is_active(brick_entity_id_t entity_id);
uint8_t audio_fx_runtime_is_comp(brick_entity_id_t entity_id);
uint8_t audio_fx_runtime_requires_stereo(brick_entity_id_t entity_id);
uint8_t audio_fx_runtime_pre_filter_supported(brick_entity_id_t entity_id);
audio_fx_filter_pos_t audio_fx_runtime_get_filter_pos(brick_entity_id_t entity_id);
uint8_t audio_fx_runtime_set_filter_pos(brick_entity_id_t entity_id,
                                        audio_fx_filter_pos_t position);
uint8_t audio_fx_runtime_set_order(brick_entity_id_t entity_id,
                                   audio_fx_order_t order);
uint8_t audio_fx_runtime_set_spatial_mode(brick_entity_id_t entity_id,
                                          audio_fx_slot_t slot, uint8_t mode);
void audio_fx_runtime_rebuild_entity_plan(brick_entity_id_t entity_id);
audio_fx_placement_t audio_fx_runtime_get_placement(brick_entity_id_t entity_id);
uint8_t audio_fx_runtime_apply_param(brick_entity_id_t entity_id,
                                     param_id_t id,
                                     float value);
uint8_t audio_fx_runtime_apply_drift_delay_modulated(brick_entity_id_t entity_id,
                                                     param_id_t id,
                                                     float value);
void audio_fx_runtime_process_mono(brick_entity_id_t entity_id,
                                   float *buffer,
                                   uint32_t frames);
void audio_fx_runtime_process_stereo(brick_entity_id_t entity_id,
                                     float *left,
                                     float *right,
                                     uint32_t frames);
void audio_fx_runtime_process_stereo_sample(brick_entity_id_t entity_id,
                                            float *left,
                                            float *right);
audio_fx_sample_plan_handle_t audio_fx_runtime_get_sample_plan(
    brick_entity_id_t entity_id);
void audio_fx_runtime_process_stereo_sample_prepared(
    audio_fx_sample_plan_handle_t plan,
    float *left,
    float *right);
void audio_fx_runtime_process_before_filter(brick_entity_id_t entity_id,
                                            float *left,
                                            float *right,
                                            uint32_t frames);
void audio_fx_runtime_process_after_filter(brick_entity_id_t entity_id,
                                           float *left,
                                           float *right,
                                           uint32_t frames);
uint8_t audio_fx_runtime_process_parallel_slot(brick_entity_id_t entity_id,
                                               audio_fx_slot_t slot,
                                               float *left,
                                               float *right,
                                               uint32_t frames);
float audio_fx_runtime_process_mono_sample(brick_entity_id_t entity_id,
                                           float sample);
void audio_fx_runtime_process(brick_entity_id_t entity_id,
                               float *left,
                               float *right,
                               uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_FX_RUNTIME_H */
