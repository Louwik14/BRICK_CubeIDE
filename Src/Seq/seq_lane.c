#include "Seq/seq_lane.h"

#include "Core/track_state.h"
#include "UI/ui_core.h"

uint8_t seq_lane_group_is_active(void)
{
    return (track_state_get_type((uint8_t)SEQ_GROUP_PARENT_MAIN_TRACK)
            == UI_TRACK_TYPE_GROUP) ? 1U : 0U;
}

uint8_t seq_lane_get_descriptor(seq_lane_id_t lane,
                                seq_lane_descriptor_t *out_descriptor)
{
    return seq_lane_resolve(seq_lane_group_is_active(), lane, out_descriptor);
}
