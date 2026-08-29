#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "Seq/seq_runtime_control.h"

void seq_runtime_set_tracks_muted(const seq_track_id_t *tracks, uint8_t track_count, uint8_t muted)
{
    if (muted != 0U)
        seq_play_scheduler_suspend_tracks(tracks, track_count);
    else
        seq_play_scheduler_resume_tracks(tracks, track_count);
}
