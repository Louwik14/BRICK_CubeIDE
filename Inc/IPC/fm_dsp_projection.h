#ifndef BRICK6_FM_DSP_PROJECTION_H
#define BRICK6_FM_DSP_PROJECTION_H

#include <stdint.h>

#define TRACK_TONE_FM_OPERATOR_COUNT 6U

typedef struct
{
    uint8_t rates[4];
    uint8_t levels[4];
    uint8_t breakpoint;
    uint8_t left_depth;
    uint8_t right_depth;
    uint8_t left_curve;
    uint8_t right_curve;
    uint8_t rate_scaling;
    uint8_t output_level;
    uint8_t mode;
    uint8_t coarse;
    uint8_t fine;
    int8_t detune;
    uint8_t velocity_sensitivity;
    uint8_t enabled;
} track_tone_fm_operator_base_t;

typedef struct
{
    track_tone_fm_operator_base_t operators[TRACK_TONE_FM_OPERATOR_COUNT];
    uint8_t pitch_rates[4];
    uint8_t pitch_levels[4];
    uint8_t transpose;
    uint8_t algorithm;
    uint8_t feedback;
    uint8_t key_sync;
} track_tone_fm_base_voice_t;

typedef struct
{
    float ratio, bright, body, detail, metal;
    float env_attack, env_decay, env_sustain, env_release;
    float play_vel, play_key, pitch_env, pitch_time;
} track_tone_fm_macros_t;

#ifdef __cplusplus
static_assert(sizeof(track_tone_fm_operator_base_t) == 21U, "FM operator layout changed");
static_assert(sizeof(track_tone_fm_base_voice_t) == 138U, "FM voice layout changed");
static_assert(sizeof(track_tone_fm_macros_t) == 52U, "FM macro layout changed");
#else
_Static_assert(sizeof(track_tone_fm_operator_base_t) == 21U, "FM operator layout changed");
_Static_assert(sizeof(track_tone_fm_base_voice_t) == 138U, "FM voice layout changed");
_Static_assert(sizeof(track_tone_fm_macros_t) == 52U, "FM macro layout changed");
#endif

#endif
