#ifndef TRACK_SOUND_STATE_H
#define TRACK_SOUND_STATE_H

#include <stdint.h>

#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float rate;
    float shape;
    float trig;
    float phase;
} track_mod_lfo_state_t;

typedef struct
{
    float attack;
    float decay;
    float sustain;
    float release;
} track_mod_env3_state_t;

typedef struct
{
    uint8_t source_a;
    uint8_t source_b;
} track_mod_multi_state_t;

typedef struct
{
    uint8_t source;
    float amount;
} track_mod_slew_state_t;

typedef struct
{
    float mix_level;
    float mix_pan;
    float mix_send1;
    float mix_send2;
    float mix_mute;
    float type;
    float cutoff;
    float resonance;
    float eg_amount;
    float attack;
    float decay;
    float sustain;
    float release;
    float keytrack;
    float env_reset;
    float env_delay;
    float eq_low;
    float eq_mid;
    float eq_high;
    float vca_attack;
    float vca_decay;
    float vca_sustain;
    float vca_release;
    float vca_env_type;
    float env_retrig_filter;
    float env_retrig_vca;
    float env_retrig_mod;
    track_mod_lfo_state_t mod_lfo[MOD_LFO_COUNT_PER_TRACK];
    track_mod_multi_state_t mod_multi[2];
    track_mod_slew_state_t mod_slew[2];
    track_mod_env3_state_t mod_env3;
    track_mod_matrix_slot_t mod_matrix[MOD_MATRIX_SLOT_COUNT];
    uint8_t mod_matrix_selected_slot;
} track_sound_state_t;

void track_sound_state_init(void);
void track_sound_state_make_default(track_sound_state_t *out_state);
track_sound_state_t *track_sound_state_get(uint8_t track);
const track_sound_state_t *track_sound_state_get_const(uint8_t track);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_SOUND_STATE_H */
