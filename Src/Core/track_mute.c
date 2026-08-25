#include "Core/track_mute.h"

#include "Core/track_runtime.h"
#include "Core/track_sound_state.h"
#include "Keyboard/keyboard_engine.h"
#include "Core/entity_topology.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_types.h"
#include "Param/param_registry.h"
#include "Param/param_registry_runtime_state.h"

static uint8_t track_mute_resolve_mix_target(uint8_t track, uint8_t *out_mix_track)
{
    track_runtime_resolved_track_t resolved;
    if ((out_mix_track == NULL)
            || (track_runtime_resolve_track(track, &resolved) == 0U)
            || (resolved.descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (resolved.has_mix_target == 0U))
    {
        return 0U;
    }
    *out_mix_track = resolved.mix_track_id;
    return 1U;
}

void track_mute_init(void)
{
}

track_mute_kind_t track_mute_get_kind(uint8_t track)
{
    track_runtime_descriptor_t descriptor;
    if ((track >= SEQ_LANE_CAPACITY)
            || (track_runtime_get_descriptor(track, &descriptor) == 0U)
            || (descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (descriptor.family == TRACK_RUNTIME_FAMILY_OFF)
            || ((descriptor.topology_capabilities & TRACK_CAPABILITY_MUTE) == 0U))
    {
        return TRACK_MUTE_KIND_NONE;
    }

    if (descriptor.family == TRACK_RUNTIME_FAMILY_MIDI)
    {
        return TRACK_MUTE_KIND_MIDI;
    }
    if (descriptor.family == TRACK_RUNTIME_FAMILY_EXTERNAL)
    {
        return TRACK_MUTE_KIND_EXTERNAL;
    }
    if ((descriptor.family == TRACK_RUNTIME_FAMILY_SAMPLER)
            && (descriptor.type == TRACK_RUNTIME_TYPE_LOOPER))
    {
        return TRACK_MUTE_KIND_LOOPER;
    }
    return TRACK_MUTE_KIND_AUDIO;
}

uint8_t track_mute_is_available(uint8_t track)
{
    const track_mute_kind_t kind = track_mute_get_kind(track);
    if (kind == TRACK_MUTE_KIND_NONE)
    {
        return 0U;
    }
    if ((kind == TRACK_MUTE_KIND_MIDI) || (kind == TRACK_MUTE_KIND_FX))
    {
        return 1U;
    }
    uint8_t mix_track = 0U;
    return track_mute_resolve_mix_target(track, &mix_track);
}

uint8_t track_mute_get(uint8_t track)
{
    const track_sound_state_t *const sound = track_sound_state_get_const(track);
    return ((sound != NULL) && (sound->mix_mute >= 0.5f)) ? 1U : 0U;
}

uint8_t track_mute_is_effectively_muted(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return 0U;
    }

    if (track_mute_get(track) != 0U)
    {
        return 1U;
    }

    entity_topology_descriptor_t entity;
    if ((entity_topology_get((brick_entity_id_t)track, &entity) != 0U)
            && (entity.role == ENTITY_ROLE_GROUP_CHILD)
            && (track_mute_get((uint8_t)entity.parent_entity_id) != 0U))
    {
        return 1U;
    }

    return 0U;
}

static void track_mute_transition_lane(uint8_t track, uint8_t muted)
{
    const seq_track_id_t seq_track = (seq_track_id_t)track;
    seq_runtime_set_tracks_muted(&seq_track, 1U, muted);
}

uint8_t track_mute_set(uint8_t track, uint8_t muted)
{
    const track_mute_kind_t kind = track_mute_get_kind(track);
    if ((kind == TRACK_MUTE_KIND_NONE) || (track_mute_is_available(track) == 0U))
    {
        track_sound_state_t *const inactive = track_sound_state_get(track);
        if ((inactive != NULL) && (muted == 0U))
        {
            inactive->mix_mute = 0.0f;
            param_registry_control_shadow_set(track, PARAM_MIX_MUTE, 0.0f);
            return 1U;
        }
        return 0U;
    }

    uint8_t affected[BRICK_ENTITY_GROUP_CHILD_COUNT + 1U];
    uint8_t effective_before[BRICK_ENTITY_GROUP_CHILD_COUNT + 1U];
    uint8_t affected_count = 1U;
    affected[0] = track;
    entity_topology_descriptor_t topology;
    if ((entity_topology_get(track, &topology) != 0U)
            && (topology.role == ENTITY_ROLE_GROUP_MASTER))
    {
        for (uint8_t member = 0U;
             member < BRICK_ENTITY_GROUP_CHILD_COUNT; ++member)
        {
            brick_entity_id_t child = BRICK_ENTITY_INVALID_ID;
            if (entity_topology_group_child(track, member, &child) != 0U)
                affected[affected_count++] = child;
        }
    }
    for (uint8_t i = 0U; i < affected_count; ++i)
        effective_before[i] = track_mute_is_effectively_muted(affected[i]);

    muted = (muted != 0U) ? 1U : 0U;
    track_sound_state_t *const sound_state = track_sound_state_get(track);
    if (sound_state == NULL) return 0U;
    sound_state->mix_mute = (float)muted;
    param_registry_control_shadow_set(track, PARAM_MIX_MUTE, (float)muted);

    for (uint8_t i = 0U; i < affected_count; ++i)
    {
        const uint8_t effective_after =
            track_mute_is_effectively_muted(affected[i]);
        if ((effective_before[i] != effective_after)
                && (affected[i] != track
                    || kind == TRACK_MUTE_KIND_AUDIO
                    || kind == TRACK_MUTE_KIND_MIDI
                    || kind == TRACK_MUTE_KIND_EXTERNAL))
            track_mute_transition_lane(affected[i], effective_after);
        (void)param_registry_project_track_mute(affected[i], effective_after);
    }
    return 1U;
}

uint8_t track_mute_install_restored(uint8_t track,uint8_t muted)
{
    if (track >= SEQ_LANE_CAPACITY) return 0U;
    uint8_t affected[BRICK_ENTITY_GROUP_CHILD_COUNT + 1U];
    uint8_t affected_count=1U;affected[0]=track;
    entity_topology_descriptor_t topology;
    if ((entity_topology_get(track,&topology) != 0U)
            && (topology.role == ENTITY_ROLE_GROUP_MASTER))
        for (uint8_t member=0U;member<BRICK_ENTITY_GROUP_CHILD_COUNT;++member)
        {
            brick_entity_id_t child=BRICK_ENTITY_INVALID_ID;
            if (entity_topology_group_child(track,member,&child) != 0U)
                affected[affected_count++]=child;
        }
    track_sound_state_t *const sound=track_sound_state_get(track);
    if (sound == NULL) return 0U;
    sound->mix_mute=(muted != 0U)?1.0f:0.0f;
    param_registry_control_shadow_set(track,PARAM_MIX_MUTE,sound->mix_mute);
    for (uint8_t i=0U;i<affected_count;++i)
        track_mute_transition_lane(affected[i],track_mute_is_effectively_muted(affected[i]));
    return 1U;
}

uint8_t track_mute_should_suppress_note_on(uint8_t track)
{
    return track_mute_is_effectively_muted(track);
}
