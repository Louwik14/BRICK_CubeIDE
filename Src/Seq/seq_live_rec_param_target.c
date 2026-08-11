#include "Seq/seq_runtime_control.h"

#include "Seq/seq_model.h"

uint8_t seq_runtime_live_rec_param_resolve_write_step(seq_track_id_t track,
                                                      uint8_t set_id,
                                                      seq_param_slot_t param_slot,
                                                      seq_step_id_t *out_step)
{
    if ((out_step == 0)
        || (seq_runtime_live_rec_param_can_write(track, set_id, param_slot) == 0U))
    {
        return 0U;
    }

    seq_step_id_t play_step = 0U;
    if (seq_runtime_get_playhead_step(track, &play_step) == 0U)
    {
        return 0U;
    }

    const uint8_t length = seq_model_get_track_playback_length(track);
    *out_step = (play_step < length) ? play_step : 0U;
    return 1U;
}
