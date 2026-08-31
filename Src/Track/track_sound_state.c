#include "Mod/mod_matrix_control.h"
#include "Track/track_sound_state.h"

#include <stddef.h>
#include "Seq/seq_types.h"
#include "Platform/memory_layout.h"

SEQ_STATE_D2 static track_sound_state_t g_track_sound_state[SEQ_LANE_CAPACITY];

void track_sound_state_make_default(track_sound_state_t *state)
{
    if (state != NULL)
        mod_matrix_set_defaults(state->mod_matrix, &state->mod_matrix_selected_slot);
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
