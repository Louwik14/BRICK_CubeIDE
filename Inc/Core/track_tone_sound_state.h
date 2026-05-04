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
        float stretch_mode;
        float sync_len;
        float grain_size;
        float hop_size;
        float source_bpm;
        float ratio_q16;
        float transient_sensitivity;
        float preserve_pitch;
    } buffer;
    struct
    {
        float model;
        float coarse_frequency;
        float harmonics;
        float timbre;
        float morph;
        float lpg_response;
        float decay;
        float frequency_range;
    } plaits;
    struct
    {
        float edit;
        float fine;
        float coarse;
        float fm;
        float timbre;
        float modulation;
        float color;
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
    struct
    {
        float pitch;
        float decay;
        float fm_amount;
        float noise;
        float hp_tone;
        float mod_freq;
        float mod_decay;
        float noise_decay;
    } fm_snare;
    struct
    {
        float pitch;
        float decay;
        float pitch_sweep;
        float fm_amount;
        float mod_freq;
        float mod_decay;
        float sweep_decay;
        float start_phase;
    } fm_tom;
    struct
    {
        float rim_pitch;
        float rim_decay;
        float body_mix;
        float hp_tone;
        float rim_fm_amount;
        float body_pitch;
        float body_decay;
        float body_fm_amount;
        float mod_decay;
    } fm_rimshot;
    struct
    {
        float clap_count;
        float clap_spacing;
        float tail_decay;
        float hp_tone;
        float feedback;
        float fm_amount;
        float base_freq;
        float mod_freq;
        float mod_decay;
        float clap_decay;
    } fm_clap;
    struct
    {
        float pitch;
        float decay_short;
        float decay_long;
        float fm_amount;
        float feedback;
        float env_mix;
        float mod_decay;
        float mod_freq;
    } fm_cowbell;
    struct
    {
        float decay;
        float sustain;
        float fm_amount;
        float hp_tone;
        float feedback;
        float base_carrier;
        float base_mod;
        float mod_decay;
    } fm_cymbal;
} track_tone_sound_state_t;

void track_tone_sound_state_init(void);
track_tone_sound_state_t *track_tone_sound_state_get(uint8_t track);
const track_tone_sound_state_t *track_tone_sound_state_get_const(uint8_t track);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_TONE_SOUND_STATE_H */
