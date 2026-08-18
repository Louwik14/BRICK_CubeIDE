#ifndef FX_AUDIO_VIBE_H
#define FX_AUDIO_VIBE_H

#include <stdint.h>

#include "Core/entity_topology.h"

#define FX_AUDIO_VIBE_DELAY_SAMPLES 257U

typedef struct
{
    float phase;
    float old_speed;
    float drift;
    float wet;
    uint32_t rng;
    uint16_t count;
} fx_audio_vibe_state_t;

_Static_assert(sizeof(fx_audio_vibe_state_t) == 24U,
               "Vibe hot state size changed");

void fx_audio_vibe_reset(fx_audio_vibe_state_t *state,
                         brick_entity_id_t entity_id);
void fx_audio_vibe_prepare(fx_audio_vibe_state_t *state,
                           float drift,
                           float wet);
void fx_audio_vibe_process_stereo_sample(fx_audio_vibe_state_t *state,
                                         brick_entity_id_t entity_id,
                                         float *left,
                                         float *right);
void fx_audio_vibe_process_stereo(fx_audio_vibe_state_t *state,
                                  brick_entity_id_t entity_id,
                                  float *left,
                                  float *right,
                                  uint32_t frames);

#endif /* FX_AUDIO_VIBE_H */
