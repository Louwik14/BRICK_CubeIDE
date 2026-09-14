#pragma once

#include <stdint.h>

typedef enum
{
    FX_AUDIO_XFADE_TARGET_MASTER = 0U,
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

static inline uint8_t fx_audio_xfade_target_is_track(uint8_t target)
{
    return (uint8_t)((target >= FX_AUDIO_XFADE_TARGET_TRACK_1)
        && (target <= FX_AUDIO_XFADE_TARGET_TRACK_8));
}

static inline uint8_t fx_audio_xfade_target_for_track(uint8_t track)
{
    return (track < 8U)
        ? (uint8_t)(FX_AUDIO_XFADE_TARGET_TRACK_1 + track)
        : (uint8_t)FX_AUDIO_XFADE_TARGET_MASTER;
}

static inline uint8_t fx_audio_xfade_target_is_self(uint8_t owner_track,
                                                     uint8_t target)
{
    return (uint8_t)((owner_track < 8U)
        && (target == fx_audio_xfade_target_for_track(owner_track)));
}

static inline uint8_t fx_audio_xfade_target_default(uint8_t owner_track)
{
    return (owner_track < 8U)
        ? fx_audio_xfade_target_for_track((uint8_t)((owner_track + 1U) & 7U))
        : (uint8_t)FX_AUDIO_XFADE_TARGET_MASTER;
}

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
