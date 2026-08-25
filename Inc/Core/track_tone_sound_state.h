#ifndef TRACK_TONE_SOUND_STATE_H
#define TRACK_TONE_SOUND_STATE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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
    float ratio;
    float bright;
    float body;
    float detail;
    float metal;
    float env_attack;
    float env_decay;
    float env_sustain;
    float env_release;
    float play_vel;
    float play_key;
    float pitch_env;
    float pitch_time;
} track_tone_fm_macros_t;

#ifdef __cplusplus
static_assert(sizeof(track_tone_fm_operator_base_t) == 21U,
              "FM DX7 operator base layout changed");
static_assert(sizeof(track_tone_fm_base_voice_t) == 138U,
              "FM DX7 base voice layout changed");
static_assert(sizeof(track_tone_fm_macros_t) == 52U,
              "FM macro layout changed");
#else
_Static_assert(sizeof(track_tone_fm_operator_base_t) == 21U,
               "FM DX7 operator base layout changed");
_Static_assert(sizeof(track_tone_fm_base_voice_t) == 138U,
               "FM DX7 base voice layout changed");
_Static_assert(sizeof(track_tone_fm_macros_t) == 52U,
               "FM macro layout changed");
#endif

typedef struct
{
    float sample;
    float gain;
    float start;
    float end;
    float mode;
    float tune;
    float slice_count;
    float loop_start;
    struct
    {
        float source_bpm;
        float sync_length;
        float pitch;
        float play_mode;
        float loop;
        float stretch_mode;
        float grain_size;
        float hop_size;
        float search_size;
    } clip;
    struct
    {
        float loop;
    } multi;
    struct
    {
        float arm;
        float len;
        float play;
        float xfade;
        float stretch;
        float pitch;
        float grain;
    } looper;
    struct
    {
        float model[2];
        float pitch_mod[2];
        float param1[2];
        float amod[2];
        float param2[2];
        float phase_reset[2];
        float volume;
        float balance;
        float tune;
        float detune;
    } prism;
    struct
    {
        float level[3];
        float model[3];
        float tune[3];
        float timbre[3];
        float color[3];
        float noise_level;
        float osc_detune;
        float phase_reset;
    } stack;
    struct
    {
        float table[2];
        float pos[2];
        float volume;
        float balance;
        float tune;
        float detune;
    } wave;
    struct
    {
        track_tone_fm_base_voice_t base;
        track_tone_fm_macros_t macros;
        float operator_select;
    } fm;
    float midi_program;
    float midi_cc[12];
    struct
    {
        float pitch;
        float decay;
        float pitch_sweep;
        float sweep_decay;
        float attack;
        float noise;
        float harmonics;
        float drive;
    } trx_bd;
    struct
    {
        uint8_t model;
        uint8_t slot[8];
    } md;
} track_tone_sound_state_t;

#ifdef __cplusplus
static_assert(sizeof(track_tone_sound_state_t) == 560U,
              "track tone state layout changed");
#else
_Static_assert(sizeof(track_tone_sound_state_t) == 560U,
               "track tone state layout changed");
#endif

void track_tone_sound_state_init(void);
void track_tone_sound_state_make_default(track_tone_sound_state_t *out_state);
track_tone_sound_state_t *track_tone_sound_state_get(uint8_t track);
const track_tone_sound_state_t *track_tone_sound_state_get_const(uint8_t track);
uint8_t track_tone_sound_state_md_slot_count(uint8_t track);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_TONE_SOUND_STATE_H */
