#pragma once

#include <stdint.h>
#include "Track/audio_fx_xfade_contract.h"

typedef struct
{
    float position;
    float gain_a;
    float gain_b;
    float gain_a_current;
    float gain_b_current;
    uint8_t target;
    uint8_t curve;
} fx_audio_xfade_t;

void fx_audio_xfade_init(fx_audio_xfade_t *state);
void fx_audio_xfade_prepare(fx_audio_xfade_t *state,
                            float position,
                            uint8_t target,
                            uint8_t curve);
void fx_audio_xfade_process_block(fx_audio_xfade_t *state,
                                  float *carrier_l,
                                  float *carrier_r,
                                  const float *target_l,
                                  const float *target_r,
                                  uint32_t frames);
