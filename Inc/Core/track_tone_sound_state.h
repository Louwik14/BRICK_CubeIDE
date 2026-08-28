#ifndef TRACK_TONE_SOUND_STATE_H
#define TRACK_TONE_SOUND_STATE_H

#include <stddef.h>
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

/* Local derived view used by AUDIO backend adapters; never stored by CONTROL. */
typedef struct
{
    float sample, gain, start, length, mode, tune, slice_count, loop_start;
    struct { float source_bpm, sync_length, pitch, play_mode, loop,
                   stretch_mode, grain_size, hop_size, search_size; } clip;
    struct { float loop; } multi;
    struct { float arm, len, play, xfade, stretch, pitch, grain; } looper;
    struct { float model[2], pitch_mod[2], param1[2], amod[2], param2[2];
             float phase_reset, drift, volume, balance, tune, detune; } prism;
    struct { float level[3], model[3], tune[3], timbre[3], color[3];
             float noise_level, osc_detune, phase_reset; } stack;
    struct { float table[2], pos[2], start[2], len[2];
             float volume, balance, tune, detune; } wave;
    struct { track_tone_fm_base_voice_t base; track_tone_fm_macros_t macros;
             float operator_select; } fm;
    float midi_program, midi_cc[12];
    struct { float pitch, decay, pitch_sweep, sweep_decay, attack, noise,
                   harmonics, drive; } trx_bd;
    struct { uint8_t model, slot[8]; } md;
} track_tone_sound_state_t;

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
