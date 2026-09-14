#pragma once

#include <stdint.h>

typedef enum
{
    FX_AUDIO_XFADE_TARGET_MASTER = 0U,
    FX_AUDIO_XFADE_TARGET_REC,
    FX_AUDIO_XFADE_TARGET_TRACK_1,
    FX_AUDIO_XFADE_TARGET_TRACK_2,
    FX_AUDIO_XFADE_TARGET_TRACK_3,
    FX_AUDIO_XFADE_TARGET_TRACK_4,
    FX_AUDIO_XFADE_TARGET_TRACK_5,
    FX_AUDIO_XFADE_TARGET_TRACK_6,
    FX_AUDIO_XFADE_TARGET_TRACK_7,
    FX_AUDIO_XFADE_TARGET_TRACK_8,
    FX_AUDIO_XFADE_TARGET_LINE,
    FX_AUDIO_XFADE_TARGET_USB,
    FX_AUDIO_XFADE_TARGET_COUNT
} fx_audio_xfade_target_t;

typedef enum
{
    FX_AUDIO_XFADE_CURVE_POWER = 0U,
    FX_AUDIO_XFADE_CURVE_LINEAR,
    FX_AUDIO_XFADE_CURVE_DIP,
    FX_AUDIO_XFADE_CURVE_CUT,
    FX_AUDIO_XFADE_CURVE_TRANS,
    FX_AUDIO_XFADE_CURVE_COUNT
} fx_audio_xfade_curve_t;

typedef struct
{
    float position;
    float position_current;
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
