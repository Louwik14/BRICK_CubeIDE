#define SEQ_PLOCK_ROUTE_IMPLEMENTATION 1
#include "Seq/seq_plock_route.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Seq/seq_model.h"

uint8_t seq_plock_route_target_is_seq_link_slave(seq_track_id_t target_track)
{
    if (target_track >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(target_track, &role_u8);
    if (role_u8 != (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        return 0U;
    }

    uint8_t seq_link = 0U;
    return (uint8_t)(((track_runtime_get_voice_group_seq_link(target_track, &seq_link) != 0U)
                      && (seq_link != 0U)) ? 1U : 0U);
}

uint8_t seq_plock_route_resolve(seq_track_id_t scheduler_track,
                                seq_step_id_t scheduler_step,
                                seq_plock_route_t *out_route)
{
    if ((out_route == NULL)
            || (scheduler_track >= SEQ_TRACK_COUNT)
            || (seq_model_is_step_editable_index(scheduler_step) == 0U))
    {
        return 0U;
    }

    memset(out_route, 0, sizeof(*out_route));
    out_route->scheduler_track = scheduler_track;
    out_route->scheduler_step = scheduler_step;
    out_route->source_track = scheduler_track;
    out_route->source_step = scheduler_step;

    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(scheduler_track, &role_u8);
    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        return 1U;
    }

    uint8_t group_members[SEQ_PLOCK_ROUTE_GROUP_MEMBER_MAX];
    uint8_t group_member_count = 0U;
    const uint8_t group_master =
        (uint8_t)((role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
                && (track_runtime_collect_voice_group_members(scheduler_track,
                                                              group_members,
                                                              (uint8_t)(sizeof(group_members) / sizeof(group_members[0])),
                                                              &group_member_count) != 0U)
                && (group_member_count > 1U));
    if (group_master != 0U)
    {
        out_route->group_master = 1U;
        out_route->target_count = group_member_count;
        for (uint8_t i = 0U; i < group_member_count; ++i)
        {
            out_route->targets[i] = (seq_track_id_t)group_members[i];
        }

        uint8_t seq_link = 0U;
        if ((track_runtime_get_voice_group_seq_link(scheduler_track, &seq_link) != 0U)
                && (seq_link != 0U))
        {
            seq_track_id_t master_track = scheduler_track;
            if (track_runtime_get_voice_group_effective_master(scheduler_track, &master_track) != 0U)
            {
                out_route->source_track = master_track;
                out_route->source_step = scheduler_step;
                out_route->linked = 1U;
            }
        }
        return 1U;
    }

    out_route->targets[0] = scheduler_track;
    out_route->target_count = 1U;
    return 1U;
}
