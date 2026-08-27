#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "Seq/seq_runtime_control.h"

void seq_runtime_set_tracks_muted(const seq_track_id_t *tracks, uint8_t track_count, uint8_t muted)
{
    if (muted != 0U)
    {
        if (seq_play_scheduler_transition_tracks(
                tracks, track_count, SEQ_PLAY_TRANSITION_MUTE_TRIGS) == 0U)
            return;
    }
    else
    {
        if (seq_play_scheduler_transition_tracks(
                tracks, track_count, SEQ_PLAY_TRANSITION_RESUME_TRIGS) == 0U)
            return;
    }
}
