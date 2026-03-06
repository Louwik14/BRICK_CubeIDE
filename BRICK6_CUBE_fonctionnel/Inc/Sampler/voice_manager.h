#pragma once

#include <stdint.h>

#include "Sampler/sample_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    VOICE_OFF = 0,
    VOICE_ATTACK
} voice_state_t;

typedef struct
{
    uint16_t sample_id;
    const sample_desc_t *sample;

    uint32_t position;

    float gain_l;
    float gain_r;

    voice_state_t state;
    uint8_t active;
} voice_t;

extern voice_t voices[24];

void voice_manager_init(void);
void voice_manager_trigger(uint16_t sample_id, float gain_l, float gain_r);
void voice_manager_process(float *out_l, float *out_r, uint32_t frames);

#ifdef __cplusplus
}
#endif
