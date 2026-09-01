#include "Track/track_state.h"

#include <string.h>

#include "Track/track_input_ownership.h"
#include "Track/track_catalog.h"
#include "Track/track_runtime.h"
#include "Track/entity_topology.h"
#include "Track/track_mute.h"

static track_config_t g_track_configs[TRACK_CONFIG_CAPACITY];
static uint8_t g_track_midi_channel[TRACK_CONFIG_CAPACITY];
static track_midi_source_t g_track_midi_source[TRACK_CONFIG_CAPACITY];
static uint32_t g_track_revision[TRACK_CONFIG_CAPACITY];
static uint32_t g_track_state_global_revision = 0U;

static track_config_t track_state_default_config(void)
{
    track_config_t config = {
        .family = TRACK_FAMILY_OFF,
        .type = TRACK_TYPE_NONE,
    };

    return config;
}

static track_config_t track_state_initial_config(uint8_t track)
{
    entity_topology_descriptor_t entity;
    if ((entity_topology_resolve(1U, (brick_entity_id_t)track, &entity) != 0U)
            && (entity.role == ENTITY_ROLE_GROUP_CHILD))
    {
        return (track_config_t){
            .family = TRACK_FAMILY_SAMPLER,
            .type = TRACK_TYPE_RAM,
        };
    }
    return track_state_default_config();
}

static void track_state_bump_revision(uint8_t track)
{
    if (track >= TRACK_CONFIG_CAPACITY)
    {
        return;
    }

    ++g_track_state_global_revision;
    ++g_track_revision[track];
}

static void track_state_commit_entry(uint8_t track,
                                     const track_config_t *next_config,
                                     uint8_t next_midi_channel,
                                     track_midi_source_t next_midi_source)
{
    if ((track >= TRACK_CONFIG_CAPACITY) || (next_config == NULL))
    {
        return;
    }

    if ((g_track_configs[track].family != next_config->family)
            || (g_track_configs[track].type != next_config->type)
            || (g_track_midi_channel[track] != next_midi_channel)
            || (g_track_midi_source[track] != next_midi_source))
    {
        g_track_configs[track] = *next_config;
        g_track_midi_channel[track] = next_midi_channel;
        g_track_midi_source[track] = next_midi_source;
        track_state_bump_revision(track);
    }
}

void track_state_init(void)
{
    for (uint8_t track = 0U; track < TRACK_CONFIG_CAPACITY; ++track)
    {
        g_track_configs[track] = track_state_initial_config(track);
        g_track_midi_channel[track] = (uint8_t)((track < 16U) ? (track + 1U) : 16U);
        g_track_midi_source[track] = TRACK_MIDI_SOURCE_ALL;
        g_track_revision[track] = 0U;
    }

    g_track_state_global_revision = 1U;
    track_input_ownership_init(g_track_configs);
}

const track_config_t *track_state_get_configs(void)
{
    return &g_track_configs[0];
}

track_config_t track_state_get_config(uint8_t track)
{
    if (track >= TRACK_CONFIG_CAPACITY)
    {
        return track_state_default_config();
    }

    return g_track_configs[track];
}

track_family_t track_state_get_family(uint8_t track)
{
    return track_state_get_config(track).family;
}

track_type_t track_state_get_type(uint8_t track)
{
    return track_state_get_config(track).type;
}

uint8_t track_state_get_midi_channel(uint8_t track)
{
    if (track >= TRACK_CONFIG_CAPACITY)
    {
        return 1U;
    }

    const uint8_t channel = g_track_midi_channel[track];
    return (channel < 1U) ? 1U : ((channel > 16U) ? 16U : channel);
}

track_midi_source_t track_state_get_midi_source(uint8_t track)
{
    if (track >= TRACK_CONFIG_CAPACITY)
    {
        return TRACK_MIDI_SOURCE_ALL;
    }

    const uint8_t source = (uint8_t)g_track_midi_source[track];
    if (source >= (uint8_t)TRACK_MIDI_SOURCE_COUNT)
    {
        return TRACK_MIDI_SOURCE_ALL;
    }

    return (track_midi_source_t)source;
}

bool track_state_set_track_midi_channel(uint8_t track, uint8_t channel_1_16)
{
    if ((track >= TRACK_CONFIG_CAPACITY) || (channel_1_16 < 1U) || (channel_1_16 > 16U))
    {
        return false;
    }

    if (g_track_midi_channel[track] == channel_1_16)
    {
        return true;
    }

    g_track_midi_channel[track] = channel_1_16;
    track_state_bump_revision(track);
    return true;
}

bool track_state_set_track_midi_source(uint8_t track, track_midi_source_t source)
{
    if ((track >= TRACK_CONFIG_CAPACITY) || ((uint8_t)source >= (uint8_t)TRACK_MIDI_SOURCE_COUNT))
    {
        return false;
    }

    if (g_track_midi_source[track] == source)
    {
        return true;
    }

    g_track_midi_source[track] = source;
    track_state_bump_revision(track);
    return true;
}

uint8_t track_state_get_external_input(uint8_t track)
{
    return track_input_ownership_get_external_input(track);
}

bool track_state_set_external_input(uint8_t track, uint8_t input)
{
    if ((track >= TRACK_COUNT)
            || (entity_topology_is_active(track) == 0U)
            || (track_input_ownership_set_external_input(
                    track, input, g_track_configs) == 0U))
    {
        return false;
    }
    track_state_bump_revision(track);
    return true;
}

bool track_structure_apply_bulk(const uint8_t family[TRACK_COUNT],
                                const uint8_t type[TRACK_COUNT],
                                const uint8_t midi_channel[TRACK_COUNT],
                                const uint8_t midi_source[TRACK_COUNT])
{
    uint8_t entity_family[BRICK_ENTITY_CAPACITY];
    uint8_t entity_type[BRICK_ENTITY_CAPACITY];
    uint8_t entity_channel[BRICK_ENTITY_CAPACITY];
    uint8_t entity_source[BRICK_ENTITY_CAPACITY];
    uint8_t external_input[TRACK_COUNT];
    if ((family == NULL) || (type == NULL) || (midi_channel == NULL)
            || (midi_source == NULL))
        return false;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        const track_config_t config = track_state_get_config(entity);
        entity_family[entity] = (entity < TRACK_COUNT)
            ? family[entity] : (uint8_t)config.family;
        entity_type[entity] = (entity < TRACK_COUNT)
            ? type[entity] : (uint8_t)config.type;
        entity_channel[entity] = (entity < TRACK_COUNT)
            ? midi_channel[entity] : track_state_get_midi_channel(entity);
        entity_source[entity] = (entity < TRACK_COUNT)
            ? midi_source[entity] : (uint8_t)track_state_get_midi_source(entity);
        if (entity < TRACK_COUNT)
            external_input[entity] = track_state_get_external_input(entity);
    }
    return track_structure_apply_entity_bulk_with_inputs(
        entity_family, entity_type, entity_channel, entity_source,
        external_input);
}

static bool track_state_apply_entity_bulk_with_inputs(
    const uint8_t family[BRICK_ENTITY_CAPACITY],
    const uint8_t type[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_channel[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_source[BRICK_ENTITY_CAPACITY],
    const uint8_t external_input[TRACK_COUNT],
    const uint8_t effective_mute_before[BRICK_ENTITY_CAPACITY])
{
    if ((family == NULL) || (type == NULL) || (midi_channel == NULL)
            || (midi_source == NULL) || (external_input == NULL)
            || (effective_mute_before == NULL))
    {
        return false;
    }

    track_config_t next_configs[TRACK_CONFIG_CAPACITY];
    uint8_t next_channels[TRACK_CONFIG_CAPACITY];
    track_midi_source_t next_sources[TRACK_CONFIG_CAPACITY];

    for (uint8_t track = 0U; track < TRACK_CONFIG_CAPACITY; ++track)
    {
        next_configs[track] = track_state_get_config(track);
        next_channels[track] = track_state_get_midi_channel(track);
        next_sources[track] = track_state_get_midi_source(track);
    }

    for (uint8_t track = 0U; track < TRACK_CONFIG_CAPACITY; ++track)
    {
        const track_family_t fam = (track_family_t)family[track];
        track_type_t typ = (track_type_t)type[track];
        const track_midi_source_t src = (track_midi_source_t)midi_source[track];

        if (((uint8_t)fam >= (uint8_t)TRACK_FAMILY_COUNT)
                || ((uint8_t)src >= (uint8_t)TRACK_MIDI_SOURCE_COUNT))
        {
            return false;
        }

        if (fam == TRACK_FAMILY_OFF)
        {
            next_configs[track].family = TRACK_FAMILY_OFF;
            next_configs[track].type = TRACK_TYPE_NONE;
        }
        else
        {
            track_config_t normalized = {
                .family = fam,
                .type = typ
            };
            if ((normalized.family != TRACK_FAMILY_OFF)
                    && !track_catalog_type_is_valid_for_family(normalized.family, normalized.type))
            {
                normalized.type = track_catalog_default_type_for_family(normalized.family);
                if (!track_catalog_type_is_valid_for_family(normalized.family, normalized.type))
                {
                    return false;
                }
            }

            next_configs[track].family = normalized.family;
            next_configs[track].type = normalized.type;
        }
        next_channels[track] = (midi_channel[track] < 1U)
            ? 1U
            : ((midi_channel[track] > 16U) ? 16U : midi_channel[track]);
        next_sources[track] = src;
    }

    const uint8_t group_active = (uint8_t)(
        next_configs[BRICK_ENTITY_GROUP_MASTER_ID].type == TRACK_TYPE_GROUP);
    for (uint8_t track = 0U; track < TRACK_CONFIG_CAPACITY; ++track)
    {
        entity_topology_descriptor_t entity;
        if ((entity_topology_resolve(group_active, track, &entity) == 0U)
                || (entity.active == 0U))
        {
            continue;
        }
        const track_family_t fam = next_configs[track].family;
        const track_type_t typ = next_configs[track].type;

        if ((fam != TRACK_FAMILY_OFF)
                && !track_catalog_family_is_available(track, fam, next_configs))
        {
            return false;
        }

        if ((fam != TRACK_FAMILY_OFF)
                && !track_catalog_type_is_available(track, fam, typ, next_configs))
        {
            return false;
        }
    }

    if (track_input_ownership_apply_bulk(next_configs, external_input) == 0U)
    {
        return false;
    }

    if (track_mute_publish_topology_projection(
            group_active, effective_mute_before) == 0U)
    {
        return false;
    }

    for (uint8_t track = 0U; track < TRACK_CONFIG_CAPACITY; ++track)
    {
        track_state_commit_entry(track, &next_configs[track], next_channels[track], next_sources[track]);
    }


    return true;
}

bool track_structure_apply_entity_bulk_with_inputs(
    const uint8_t family[BRICK_ENTITY_CAPACITY],
    const uint8_t type[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_channel[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_source[BRICK_ENTITY_CAPACITY],
    const uint8_t external_input[TRACK_COUNT])
{
    uint8_t effective_mute_before[BRICK_ENTITY_CAPACITY];
    uint32_t revision_before[BRICK_ENTITY_CAPACITY];
    const uint8_t group_active_before = entity_topology_group_is_active();
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        const int8_t effective_mute =
            track_mute_is_effectively_muted(entity);
        if (effective_mute < 0) return false;
        effective_mute_before[entity] = (uint8_t)effective_mute;
        revision_before[entity] = track_state_get_revision(entity);
    }
    if (!track_state_apply_entity_bulk_with_inputs(
            family, type, midi_channel, midi_source, external_input,
            effective_mute_before))
        return false;
    if (group_active_before != entity_topology_group_is_active())
    {
        track_runtime_rebuild_all();
    }
    else
    {
        for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
            if (revision_before[entity] != track_state_get_revision(entity))
                track_runtime_rebuild_track(entity);
    }
    track_mute_apply_topology_change(effective_mute_before);
    return true;
}

uint8_t track_state_count_tracks_with_family(track_family_t family)
{
    if ((uint8_t)family >= (uint8_t)TRACK_FAMILY_COUNT)
    {
        return 0U;
    }

    uint8_t count = 0U;
    for (uint8_t track = 0U; track < TRACK_CONFIG_CAPACITY; ++track)
    {
        if ((entity_topology_is_active(track) != 0U)
                && (g_track_configs[track].family == family))
        {
            ++count;
        }
    }

    return count;
}

uint32_t track_state_get_revision(uint8_t track)
{
    if (track >= TRACK_CONFIG_CAPACITY)
    {
        return 0U;
    }

    return g_track_revision[track];
}

uint32_t track_state_get_global_revision(void)
{
    return g_track_state_global_revision;
}
