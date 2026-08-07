#ifndef SEQ_LANE_H
#define SEQ_LANE_H

#include <stdint.h>

#include "Seq/seq_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SEQ_LANE_ROLE_MAIN = 0,
    SEQ_LANE_ROLE_GROUP_MASTER,
    SEQ_LANE_ROLE_GROUP_CHILD
} seq_lane_role_t;

typedef struct
{
    seq_lane_id_t lane_id;
    uint8_t main_track_id;
    uint8_t child_index;
    seq_lane_id_t parent_lane_id;
    seq_lane_role_t role;
    uint8_t active;
    uint8_t can_sequence;
    uint8_t can_emit_notes;
    uint8_t has_audio_source;
} seq_lane_descriptor_t;

static inline uint8_t seq_lane_is_valid(seq_lane_id_t lane)
{
    return (lane < (seq_lane_id_t)SEQ_LANE_CAPACITY) ? 1U : 0U;
}

static inline seq_lane_id_t seq_lane_for_main_track(uint8_t track)
{
    return (track < (uint8_t)SEQ_MAIN_TRACK_COUNT) ? (seq_lane_id_t)track : UINT8_MAX;
}

static inline seq_lane_id_t seq_lane_for_group_child(uint8_t child_index)
{
    return (child_index < (uint8_t)SEQ_GROUP_SUBTRACK_COUNT)
            ? (seq_lane_id_t)(SEQ_GROUP_FIRST_CHILD_LANE + child_index)
            : UINT8_MAX;
}

static inline uint8_t seq_lane_resolve(uint8_t group_active,
                                       seq_lane_id_t lane,
                                       seq_lane_descriptor_t *out_descriptor)
{
    if ((out_descriptor == 0) || (seq_lane_is_valid(lane) == 0U))
    {
        return 0U;
    }

    out_descriptor->lane_id = lane;
    out_descriptor->main_track_id = UINT8_MAX;
    out_descriptor->child_index = UINT8_MAX;
    out_descriptor->parent_lane_id = UINT8_MAX;
    out_descriptor->role = SEQ_LANE_ROLE_MAIN;
    out_descriptor->active = 0U;
    out_descriptor->can_sequence = 0U;
    out_descriptor->can_emit_notes = 0U;
    out_descriptor->has_audio_source = 0U;

    if (lane < (seq_lane_id_t)SEQ_MAIN_TRACK_COUNT)
    {
        out_descriptor->main_track_id = lane;
        out_descriptor->active = 1U;
        out_descriptor->can_sequence = 1U;
        out_descriptor->can_emit_notes = 1U;
        out_descriptor->has_audio_source = 1U;

        if ((group_active != 0U)
                && (lane == (seq_lane_id_t)SEQ_GROUP_PARENT_MAIN_TRACK))
        {
            out_descriptor->role = SEQ_LANE_ROLE_GROUP_MASTER;
            out_descriptor->can_emit_notes = 0U;
            out_descriptor->has_audio_source = 0U;
        }

        return 1U;
    }

    if (group_active == 0U)
    {
        return 1U;
    }

    out_descriptor->role = SEQ_LANE_ROLE_GROUP_CHILD;
    out_descriptor->child_index = (uint8_t)(lane - (seq_lane_id_t)SEQ_GROUP_FIRST_CHILD_LANE);
    out_descriptor->parent_lane_id = (seq_lane_id_t)SEQ_GROUP_PARENT_MAIN_TRACK;
    out_descriptor->active = 1U;
    out_descriptor->can_sequence = 1U;
    out_descriptor->can_emit_notes = 1U;
    out_descriptor->has_audio_source = 1U;
    return 1U;
}

#ifdef __cplusplus
}
#endif

#endif /* SEQ_LANE_H */
