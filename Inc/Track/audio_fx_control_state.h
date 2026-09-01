#ifndef AUDIO_FX_CONTROL_STATE_H
#define AUDIO_FX_CONTROL_STATE_H

#include <stdint.h>
#include "Param/engine_model_catalog.h"
#include "Param/param_ids.h"
#include "Track/entity_types.h"
#include "App/live_parameter_audio_publication.h"

typedef struct
{
    audio_fx_filter_pos_t filter_position;
    audio_fx_order_t order;
    uint8_t spatial_mode[AUDIO_FX_SLOT_COUNT];
} audio_fx_control_config_t;

typedef struct
{
    audio_fx_control_config_t config;
    uint8_t model[2U];
    float p1[2U], p2[2U], p3[2U];
    float group_level[2U];
} audio_fx_control_state_t;

typedef struct
{
    uint8_t initialized;
    uint8_t finalized;
    uint8_t model[2U];
} audio_fx_control_prepare_context_t;

void audio_fx_control_state_init(void);
uint8_t audio_fx_control_state_reset(brick_entity_id_t entity);
uint8_t audio_fx_control_state_get(brick_entity_id_t entity,
                                   audio_fx_control_config_t *out);
uint8_t audio_fx_control_set_filter_position(brick_entity_id_t entity,
                                             audio_fx_filter_pos_t position);
uint8_t audio_fx_control_set_order(brick_entity_id_t entity, audio_fx_order_t order);
uint8_t audio_fx_control_set_spatial_mode(brick_entity_id_t entity,
                                          audio_fx_slot_t slot, uint8_t mode);
uint8_t audio_fx_control_state_get_param(brick_entity_id_t entity,
                                         param_id_t id, float *out_value);
uint8_t audio_fx_control_state_capture(brick_entity_id_t entity,
                                       audio_fx_control_state_t *out_state);
uint8_t audio_fx_control_state_restore(brick_entity_id_t entity,
                                       const audio_fx_control_state_t *state);
uint8_t audio_fx_control_state_prepare_for_polyphony(
    brick_entity_id_t entity, const audio_fx_control_state_t *state,
    uint8_t candidate_voice_count, audio_fx_control_state_t *out_prepared);
uint8_t audio_fx_control_state_bulk_add_prepared(
    brick_entity_id_t entity, const audio_fx_control_state_t *prepared,
    live_parameter_audio_bulk_t *bulk);
uint8_t audio_fx_control_state_install_prepared(
    brick_entity_id_t entity, const audio_fx_control_state_t *prepared);
uint8_t audio_fx_control_state_validate(const audio_fx_control_state_t *state);
uint8_t audio_fx_control_prepare_context_init(
    brick_entity_id_t entity, audio_fx_control_prepare_context_t *context);
uint8_t audio_fx_control_prepare_param(
    brick_entity_id_t entity, param_id_t id, float value,
    audio_fx_control_prepare_context_t *context, float *out_value);
uint8_t audio_fx_control_prepare_project_model(
    brick_entity_id_t entity, param_id_t id, float value,
    audio_fx_control_prepare_context_t *context);
uint8_t audio_fx_control_prepare_finalize(
    brick_entity_id_t entity, audio_fx_control_prepare_context_t *context);
uint8_t audio_fx_control_install_prepared_param(
    brick_entity_id_t entity, param_id_t id, float canonical_value);

#endif
