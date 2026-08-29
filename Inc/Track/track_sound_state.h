#ifndef TRACK_SOUND_STATE_H
#define TRACK_SOUND_STATE_H

#include <stdint.h>
#include "Mod/mod_matrix.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Structural CONTROL state only: Matrix routes are objects, not PARAM values. */
typedef struct
{
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

#endif
