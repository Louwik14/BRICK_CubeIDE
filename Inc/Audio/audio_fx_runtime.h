#ifndef AUDIO_FX_RUNTIME_H
#define AUDIO_FX_RUNTIME_H

#include <stdint.h>

#include "Core/entity_topology.h"
#include "Param/param_store.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    AUDIO_FX_MODEL_OFF = 0U,
    AUDIO_FX_MODEL_LOFI = 1U,
    AUDIO_FX_MODEL_FOLD = 2U,
    AUDIO_FX_MODEL_DRIVE = 3U,
    /* ID 4 was COMP and remains retired for persisted-project safety. */
    AUDIO_FX_MODEL_POINT = 5U,
    /* IDs 6 and 7 are retired. */
    AUDIO_FX_MODEL_SUB = 8U,
    /* ID 9 is retired. */
    AUDIO_FX_MODEL_RING = 10U,
    AUDIO_FX_MODEL_SUB_LIGHT = 11U
};

static inline uint8_t audio_fx_lofi_model_index_from_control(uint8_t p3_control)
{
    const uint16_t index = ((uint16_t)p3_control * 3U) >> 7U;
    return (index < 3U) ? (uint8_t)index : 2U;
}
static inline uint8_t audio_fx_ring_wave_index_from_control(uint8_t v){const uint16_t i=((uint16_t)v*4U)>>7U;return i<4U?(uint8_t)i:3U;}
static inline uint8_t audio_fx_ring_model_index_from_control(uint8_t v){const uint16_t i=((uint16_t)v*6U)>>7U;return i<6U?(uint8_t)i:5U;}

typedef enum
{
    AUDIO_FX_PLACEMENT_PRE_FILTER = 0U,
    AUDIO_FX_PLACEMENT_POST_FILTER = 1U
} audio_fx_placement_t;

void audio_fx_runtime_init(void);
uint8_t audio_fx_runtime_is_param(param_id_t id);
uint8_t audio_fx_runtime_is_active(brick_entity_id_t entity_id);
uint8_t audio_fx_runtime_is_comp(brick_entity_id_t entity_id);
uint8_t audio_fx_runtime_requires_stereo(brick_entity_id_t entity_id);
uint8_t audio_fx_runtime_pre_filter_supported(brick_entity_id_t entity_id);
audio_fx_placement_t audio_fx_runtime_get_placement(brick_entity_id_t entity_id);
uint8_t audio_fx_runtime_apply_param(brick_entity_id_t entity_id,
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
