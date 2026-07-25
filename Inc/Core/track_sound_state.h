#ifndef TRACK_SOUND_STATE_H
#define TRACK_SOUND_STATE_H

#include <stdint.h>

#include "Mod/mod_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float rate;
    float shape;
    float delay;
    float trig;
    float fade;
    float phase_slew;
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
    float mix_level;
    float mix_pan;
    float mix_send1;
    float mix_send2;
    float mix_mute;
    struct
    {
        float hybrid_gate;
    } input;
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
    track_mod_lfo_state_t mod_lfo[2];
    track_mod_env3_state_t mod_env3;
    track_mod_matrix_slot_t mod_matrix[MOD_MATRIX_SLOT_COUNT];
    uint8_t mod_matrix_selected_slot;
} track_sound_state_t;

void track_sound_state_init(void);
track_sound_state_t *track_sound_state_get(uint8_t track);
const track_sound_state_t *track_sound_state_get_const(uint8_t track);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_SOUND_STATE_H */
