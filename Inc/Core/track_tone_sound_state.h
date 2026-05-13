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
        float grain_size;
        float preserve_pitch;
    } buffer;
    struct
    {
        float arm;
        float len;
        float play;
    } looper;
    struct
    {
        float type[4];
        float level[4];
        float macro_a[4];
        float macro_b[4];
    } master_fx;
    struct
    {
        float patch;
        float index;
        float time;
    } opal;
    struct
    {
        float edit;
        float fine;
        float coarse;
        float fm;
        float timbre;
        float modulation;
        float color;
        float phase_reset;
    } braids;
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
} track_tone_sound_state_t;

void track_tone_sound_state_init(void);
track_tone_sound_state_t *track_tone_sound_state_get(uint8_t track);
const track_tone_sound_state_t *track_tone_sound_state_get_const(uint8_t track);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_TONE_SOUND_STATE_H */
