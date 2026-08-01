#ifndef TRACK_TONE_SOUND_STATE_H
#define TRACK_TONE_SOUND_STATE_H

#include <stdint.h>
#include <stddef.h>

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
        float type[4];
        float level[4];
        float macro_a[4];
        float macro_b[4];
    } macro_fx;
    struct
    {
        float edit[2];
        float fine[2];
        float coarse[2];
        float fm[2];
        float timbre[2];
        float modulation[2];
        float color[2];
        float phase_reset[2];
        float level[2];
    } prism;
    struct
    {
        float level[3];
        float model[3];
        float tune[3];
        float timbre[3];
        float color[3];
        float param3[3];
        float noise_level;
        float osc_detune;
        float phase_reset;
    } stack;
    struct
    {
        float table[2];
        float pos[2];
        float start[2];
        float end[2];
        float level[2];
        float tune[2];
        float phase[2];
        float flip[2];
        float frame_interp;
        float sample_interp;
        float pos_update;
        float pos_smooth;
    } wave;
    struct
    {
        float model;
        float level;
        float tune;
        float fine;
        float width;
        float phase;
        float retrig;
    } deluge;
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

_Static_assert(offsetof(track_tone_sound_state_t, macro_fx) == 100U,
               "MacroFX state offset changed");
_Static_assert(sizeof(((track_tone_sound_state_t *)0)->macro_fx) == 64U,
               "MacroFX state size changed");
_Static_assert(sizeof(track_tone_sound_state_t) == 524U,
               "track tone state layout changed");

void track_tone_sound_state_init(void);
void track_tone_sound_state_make_default(track_tone_sound_state_t *out_state);
track_tone_sound_state_t *track_tone_sound_state_get(uint8_t track);
const track_tone_sound_state_t *track_tone_sound_state_get_const(uint8_t track);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_TONE_SOUND_STATE_H */
