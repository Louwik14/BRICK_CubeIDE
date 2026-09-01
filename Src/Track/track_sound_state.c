#include "Mod/mod_matrix_control.h"
#include "Track/track_sound_state.h"

#include <stddef.h>
#include "Seq/seq_types.h"
#include "Platform/memory_layout.h"

SEQ_STATE_D2 static track_sound_state_t g_track_sound_state[SEQ_LANE_CAPACITY];

void track_sound_state_make_default(track_sound_state_t *state)
{
    if (state == NULL) return;
    mod_matrix_set_defaults(state->mod_matrix, &state->mod_matrix_selected_slot);
    state->mod_multi_source[0U][0U] = (uint8_t)MOD_MATRIX_SOURCE_LFO1;
    state->mod_multi_source[0U][1U] = (uint8_t)MOD_MATRIX_SOURCE_LFO2;
    state->mod_multi_source[1U][0U] = (uint8_t)MOD_MATRIX_SOURCE_LFO1;
    state->mod_multi_source[1U][1U] = (uint8_t)MOD_MATRIX_SOURCE_ENV3;
    state->mod_slew_source[0U] = (uint8_t)MOD_MATRIX_SOURCE_LFO1;
    state->mod_slew_source[1U] = (uint8_t)MOD_MATRIX_SOURCE_LFO2;
    state->mod_slew_amount[0U] = 0.0f;
    state->mod_slew_amount[1U] = 0.0f;
}

void track_sound_state_init(void)
{
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        track_sound_state_make_default(&g_track_sound_state[track]);
}

track_sound_state_t *track_sound_state_get(uint8_t track)
{
    return (track < SEQ_LANE_CAPACITY) ? &g_track_sound_state[track] : NULL;
}

const track_sound_state_t *track_sound_state_get_const(uint8_t track)
{
    return track_sound_state_get(track);
}

uint8_t track_sound_state_capture(uint8_t track,track_sound_state_t*out_state)
{const track_sound_state_t*s=track_sound_state_get_const(track);if(s==0||out_state==0)return 0U;*out_state=*s;return 1U;}
