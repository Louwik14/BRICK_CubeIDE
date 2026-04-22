#ifndef TRACK_TONE_SOUND_STATE_H
#define TRACK_TONE_SOUND_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float sample;
    float gain;
    float start;
    float end;
    float mode;
    float tune;
    float fade_in;
    float fade_out;
    float slice_count;
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
        float pitch;
        float interval;
        float decay;
        float balance;
        float drive;
    } trx_claves;
    struct
    {
        float decay;
        float metal;
        float hp_tone;
        float lp_tone;
        float gap;
        float peak;
    } trx_hihat;
    struct
    {
        float pitch;
        float decay;
        float fm_amount;
        float pitch_sweep;
        float feedback;
        float mod_freq;
        float mod_decay;
        float sweep_decay;
        float ratio_mode;
        float ratio_index;
        float mod_env_sync;
    } fm_kick;
} track_tone_sound_state_t;

void track_tone_sound_state_init(void);
track_tone_sound_state_t *track_tone_sound_state_get(uint8_t track);
const track_tone_sound_state_t *track_tone_sound_state_get_const(uint8_t track);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_TONE_SOUND_STATE_H */
