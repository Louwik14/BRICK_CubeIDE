#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_engine.h"
#include "Seq/seq_runtime_control.h"

void seq_runtime_set_tracks_muted(const seq_track_id_t *tracks, uint8_t track_count, uint8_t muted)
{
    (void)muted;
    for(uint8_t i=0U;i<track_count;++i)seq_engine_control_disarm_track(tracks[i]);
    seq_engine_control_mark_dirty();
}
