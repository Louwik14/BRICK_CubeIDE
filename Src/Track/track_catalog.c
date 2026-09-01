#include "Track/track_catalog.h"

#include "Track/entity_topology.h"

static const track_type_t k_synth_types[] = {
    TRACK_TYPE_PRISM, TRACK_TYPE_WAVE, TRACK_TYPE_STACK, TRACK_TYPE_FM
};
static const track_type_t k_sampler_types[] = {
    TRACK_TYPE_RAM, TRACK_TYPE_STREAM, TRACK_TYPE_LOOPER,
    TRACK_TYPE_MULTI, TRACK_TYPE_GROUP
};
static const track_type_t k_midi_types[] = { TRACK_TYPE_MIDI };
static const track_type_t k_external_types[] = { TRACK_TYPE_EXTERNAL };
static const track_type_t k_drum_types[] = {
    TRACK_TYPE_DRUM_MD, TRACK_TYPE_DRUM_BD_ANALOG
};

static const track_type_t *track_catalog_types_for_family(
    track_family_t family, uint8_t *out_count)
{
    if (out_count == 0)
    {
        return 0;
    }

    switch (family)
    {
        case TRACK_FAMILY_SYNTH:
            *out_count = (uint8_t)(sizeof(k_synth_types) / sizeof(k_synth_types[0]));
            return k_synth_types;
        case TRACK_FAMILY_SAMPLER:
            *out_count = (uint8_t)(sizeof(k_sampler_types) / sizeof(k_sampler_types[0]));
            return k_sampler_types;
        case TRACK_FAMILY_DRUM:
            *out_count = (uint8_t)(sizeof(k_drum_types) / sizeof(k_drum_types[0]));
            return k_drum_types;
        case TRACK_FAMILY_MIDI:
            *out_count = (uint8_t)(sizeof(k_midi_types) / sizeof(k_midi_types[0]));
            return k_midi_types;
        case TRACK_FAMILY_EXTERNAL:
            *out_count = (uint8_t)(sizeof(k_external_types) / sizeof(k_external_types[0]));
            return k_external_types;
        case TRACK_FAMILY_OFF:
        default:
            *out_count = 0U;
            return 0;
    }
}

static uint8_t track_catalog_track_uses_type(
    uint8_t track, track_family_t family, track_type_t type,
    const track_config_t configs[BRICK_ENTITY_CAPACITY])
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (configs == 0))
    {
        return 0U;
    }
    return (uint8_t)((configs[track].family == family)
        && (configs[track].type == type));
}

static uint8_t track_catalog_count_sampler_streams(
    uint8_t track, const track_config_t configs[BRICK_ENTITY_CAPACITY])
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (configs == 0))
    {
        return 0U;
    }

    uint8_t count = 0U;
    const uint8_t group_active = (uint8_t)(
        configs[BRICK_ENTITY_GROUP_MASTER_ID].type == TRACK_TYPE_GROUP);
    for (uint8_t other = 0U; other < BRICK_ENTITY_CAPACITY; ++other)
    {
        if (other == track)
        {
            continue;
        }

        entity_topology_descriptor_t entity;
        if ((entity_topology_resolve(group_active, other, &entity) == 0U)
                || (entity.active == 0U))
        {
            continue;
        }

        if (track_catalog_track_uses_type(other, TRACK_FAMILY_SAMPLER,
                                          TRACK_TYPE_STREAM, configs) != 0U)
        {
            ++count;
        }
    }
    return count;
}

bool track_catalog_family_is_engine(track_family_t family)
{
    return (family == TRACK_FAMILY_SYNTH)
        || (family == TRACK_FAMILY_SAMPLER)
        || (family == TRACK_FAMILY_DRUM);
}

bool track_catalog_type_is_valid_for_family(track_family_t family, track_type_t type)
{
    if (((uint8_t)family >= (uint8_t)TRACK_FAMILY_COUNT)
            || ((uint8_t)type >= (uint8_t)TRACK_TYPE_COUNT))
    {
        return false;
    }

    uint8_t count = 0U;
    const track_type_t *types = track_catalog_types_for_family(family, &count);
    for (uint8_t i = 0U; (types != 0) && (i < count); ++i)
    {
        if (types[i] == type)
        {
            return true;
        }
    }
    return false;
}

bool track_catalog_type_is_available(
    uint8_t track, track_family_t family, track_type_t type,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (track_configs == 0)
            || !track_catalog_type_is_valid_for_family(family, type))
    {
        return false;
    }

    const uint8_t group_active = (uint8_t)(
        (track_configs[BRICK_ENTITY_GROUP_MASTER_ID].type == TRACK_TYPE_GROUP)
        || ((track == BRICK_ENTITY_GROUP_MASTER_ID) && (type == TRACK_TYPE_GROUP)));
    entity_topology_descriptor_t entity;
    if (entity_topology_resolve(group_active, track, &entity) == 0U)
    {
        return false;
    }
    if ((type == TRACK_TYPE_GROUP) && (entity.role != ENTITY_ROLE_GROUP_MASTER))
    {
        return false;
    }
    if ((entity.role == ENTITY_ROLE_GROUP_CHILD)
            && (family != TRACK_FAMILY_OFF)
            && !track_catalog_family_is_engine(family))
    {
        return false;
    }

    if ((family == TRACK_FAMILY_SAMPLER) && (type == TRACK_TYPE_STREAM))
    {
        if (track_catalog_track_uses_type(track, family, type, track_configs) != 0U)
        {
            return true;
        }
        return track_catalog_count_sampler_streams(track, track_configs) < BRICK6_MAX_CLIP_TRACKS;
    }
    return family != TRACK_FAMILY_OFF;
}

bool track_catalog_family_is_available(
    uint8_t track, track_family_t family,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (track_configs == 0)
            || ((uint8_t)family >= (uint8_t)TRACK_FAMILY_COUNT))
    {
        return false;
    }
    if (family == TRACK_FAMILY_OFF)
    {
        return true;
    }
    return track_catalog_type_count_for_family(family, track, track_configs) > 0U;
}

uint8_t track_catalog_type_count_for_family(
    track_family_t family, uint8_t track,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (track_configs == 0)
            || ((uint8_t)family >= (uint8_t)TRACK_FAMILY_COUNT)
            || (family == TRACK_FAMILY_OFF))
    {
        return 0U;
    }

    uint8_t catalog_count = 0U;
    const track_type_t *catalog = track_catalog_types_for_family(family, &catalog_count);
    uint8_t count = 0U;
    for (uint8_t i = 0U; (catalog != 0) && (i < catalog_count); ++i)
    {
        if (track_catalog_type_is_available(track, family, catalog[i], track_configs))
        {
            ++count;
        }
    }
    return count;
}

track_type_t track_catalog_type_at(track_family_t family, uint8_t index)
{
    uint8_t count = 0U;
    const track_type_t *types = track_catalog_types_for_family(family, &count);
    return ((types != 0) && (index < count)) ? types[index] : TRACK_TYPE_NONE;
}

track_type_t track_catalog_first_available_type(
    track_family_t family, uint8_t track,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    uint8_t count = 0U;
    const track_type_t *types = track_catalog_types_for_family(family, &count);
    for (uint8_t i = 0U; (types != 0) && (i < count); ++i)
    {
        if (track_catalog_type_is_available(track, family, types[i], track_configs))
        {
            return types[i];
        }
    }
    return TRACK_TYPE_NONE;
}

track_type_t track_catalog_default_type_for_family(track_family_t family)
{
    return track_catalog_type_at(family, 0U);
}

const char *track_catalog_family_short_name(track_family_t family)
{
    switch (family)
    {
        case TRACK_FAMILY_OFF: return "Off";
        case TRACK_FAMILY_SYNTH: return "Syn";
        case TRACK_FAMILY_SAMPLER: return "Smp";
        case TRACK_FAMILY_DRUM: return "Drm";
        case TRACK_FAMILY_MIDI: return "MID";
        case TRACK_FAMILY_EXTERNAL: return "EXT";
        default: return "---";
    }
}
