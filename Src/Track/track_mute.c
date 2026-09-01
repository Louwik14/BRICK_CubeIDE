#include "Track/track_mute.h"

#include "Track/track_runtime.h"
#include "Keyboard/keyboard_engine.h"
#include "Track/entity_topology.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/seq_types.h"
#include "IPC/live_clock_control.h"
#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"

static uint8_t g_track_mute[SEQ_LANE_CAPACITY];

void track_mute_init(void)
{
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        g_track_mute[track] = 0U;
}

static uint8_t track_mute_resolve_mix_target(uint8_t track, uint8_t *out_mix_track)
{
    track_runtime_resolved_track_t resolved;
    if ((out_mix_track == NULL)
            || (track_runtime_resolve_track(track, &resolved) == 0U)
            || (resolved.descriptor.active == 0U)
            || (resolved.has_mix_target == 0U))
    {
        return 0U;
    }
    *out_mix_track = resolved.mix_track_id;
    return 1U;
}

track_mute_kind_t track_mute_get_kind(uint8_t track)
{
    track_runtime_descriptor_t descriptor;
    if ((track >= SEQ_LANE_CAPACITY)
            || (track_runtime_get_descriptor(track, &descriptor) == 0U)
            || (descriptor.active == 0U)
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

int8_t track_mute_get(uint8_t track)
{
    return (track < SEQ_LANE_CAPACITY) ? (int8_t)g_track_mute[track] : -1;
}

uint8_t track_mute_install(uint8_t track, uint8_t muted)
{
    if (track >= SEQ_LANE_CAPACITY) return 0U;
    g_track_mute[track] = (muted != 0U) ? 1U : 0U;
    return 1U;
}

int8_t track_mute_is_effectively_muted(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return -1;
    }

    const int8_t own_mute = track_mute_get(track);
    if (own_mute < 0) return -1;
    if (own_mute != 0)
    {
        return 1U;
    }

    entity_topology_descriptor_t entity;
    if ((entity_topology_get((brick_entity_id_t)track, &entity) != 0U)
            && (entity.role == ENTITY_ROLE_GROUP_CHILD))
    {
        const int8_t parent_mute =
            track_mute_get((uint8_t)entity.parent_entity_id);
        if (parent_mute < 0) return -1;
        if (parent_mute != 0) return 1;
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
        if (muted == 0U)
        {
            return track_mute_install(track, 0U);
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
    {
        const int8_t effective = track_mute_is_effectively_muted(affected[i]);
        if (effective < 0) return 0U;
        effective_before[i] = (uint8_t)effective;
    }

    muted = (muted != 0U) ? 1U : 0U;
    uint8_t effective_after[BRICK_ENTITY_GROUP_CHILD_COUNT + 1U];
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = affected_count
    };
    for (uint8_t i = 0U; i < affected_count; ++i)
    {
        if (affected[i] == track)
            effective_after[i] = muted;
        else
        {
            const int8_t child_local = track_mute_get(affected[i]);
            if (child_local < 0) return 0U;
            effective_after[i] = (uint8_t)((child_local != 0) || (muted != 0U));
        }
        bulk.item[i] = (live_parameter_audio_bulk_item_t){
            .parameter_id = (uint16_t)PARAM_MIX_MUTE,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = affected[i],
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
            .value = live_parameter_event_encode_float(
                (float)effective_after[i])
        };
    }
    if (!live_parameter_audio_publication_submit_bulk(&bulk)) return 0U;
    if (track_mute_install(track, muted) == 0U) return 0U;

    for (uint8_t i = 0U; i < affected_count; ++i)
    {
        if ((effective_before[i] != effective_after[i])
                && (affected[i] != track
                    || kind == TRACK_MUTE_KIND_AUDIO
                    || kind == TRACK_MUTE_KIND_MIDI
                    || kind == TRACK_MUTE_KIND_EXTERNAL))
            track_mute_transition_lane(affected[i], effective_after[i]);
    }
    return 1U;
}

int8_t track_mute_should_suppress_note_on(uint8_t track)
{
    return track_mute_is_effectively_muted(track);
}

static int8_t track_mute_effective_for_group(uint8_t track,
                                             uint8_t group_active)
{
    if ((track >= SEQ_LANE_CAPACITY) || (group_active > 1U))
        return -1;

    const int8_t own_mute = track_mute_get(track);
    if (own_mute < 0) return -1;

    entity_topology_descriptor_t entity;
    if (entity_topology_resolve(group_active, (brick_entity_id_t)track,
            &entity) == 0U)
        return -1;
    if (entity.role != ENTITY_ROLE_GROUP_CHILD)
        return own_mute;

    const int8_t parent_mute = track_mute_get(
        (uint8_t)entity.parent_entity_id);
    if (parent_mute < 0) return -1;
    return (own_mute != 0) || (parent_mute != 0);
}

uint8_t track_mute_publish_topology_projection(
    uint8_t group_active_after,
    const uint8_t effective_before[BRICK_ENTITY_CAPACITY])
{
    if ((group_active_after > 1U) || (effective_before == NULL))
        return 0U;

    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = 0U
    };
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        const int8_t effective_after = track_mute_effective_for_group(
            track, group_active_after);
        if (effective_after < 0) return 0U;
        if (effective_before[track] == effective_after)
            continue;
        if (bulk.count >= LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS)
            return 0U;
        bulk.item[bulk.count++] = (live_parameter_audio_bulk_item_t){
            .parameter_id = (uint16_t)PARAM_MIX_MUTE,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
            .value = live_parameter_event_encode_float(
                (float)(effective_after != 0))
        };
    }
    return (bulk.count == 0U)
        || live_parameter_audio_publication_submit_bulk(&bulk);
}

void track_mute_apply_topology_change(
    const uint8_t effective_before[BRICK_ENTITY_CAPACITY])
{
    if (effective_before == NULL)
        return;
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        const int8_t effective_after =
            track_mute_is_effectively_muted(track);
        if ((effective_after >= 0)
                && (effective_before[track] != effective_after))
            track_mute_transition_lane(track, (uint8_t)effective_after);
    }
}
