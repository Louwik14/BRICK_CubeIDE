#include "ui_track_catalog.h"
#include "Track/entity_topology.h"

static const track_family_t g_cfg_play_family_order[] = {
    TRACK_FAMILY_OFF,
    TRACK_FAMILY_SYNTH,
    TRACK_FAMILY_DRUM,
    TRACK_FAMILY_MIDI,
    TRACK_FAMILY_EXTERNAL,
    TRACK_FAMILY_SAMPLER,
};

uint8_t ui_track_catalog_cfg_family_order_count(void)
{
    return (uint8_t)(sizeof(g_cfg_play_family_order) / sizeof(g_cfg_play_family_order[0]));
}

track_family_t ui_track_catalog_cfg_family_order_at(uint8_t index)
{
    if (index >= ui_track_catalog_cfg_family_order_count())
    {
        return TRACK_FAMILY_OFF;
    }

    return g_cfg_play_family_order[index];
}

bool ui_track_catalog_cfg_family_order_index(track_family_t family, uint8_t *out_index)
{
    if (out_index == 0)
    {
        return false;
    }

    for (uint8_t index = 0U; index < ui_track_catalog_cfg_family_order_count(); ++index)
    {
        if (g_cfg_play_family_order[index] == family)
        {
            *out_index = index;
            return true;
        }
    }

    return false;
}

static const track_type_t *ui_track_catalog_get_types_for_family(track_family_t family, uint8_t *out_count)
{
    static const track_type_t k_synth_types[] = {
        TRACK_TYPE_PRISM,
        TRACK_TYPE_WAVE,
        TRACK_TYPE_STACK,
        TRACK_TYPE_FM,
    };
    static const track_type_t k_sampler_types[] = {
        TRACK_TYPE_RAM,
        TRACK_TYPE_STREAM,
        TRACK_TYPE_LOOPER,
        TRACK_TYPE_MULTI,
        TRACK_TYPE_GROUP
    };
    static const track_type_t k_midi_types[] = { TRACK_TYPE_MIDI };
    static const track_type_t k_external_types[] = { TRACK_TYPE_EXTERNAL };
    static const track_type_t k_drum_types[] = {
        TRACK_TYPE_DRUM_MD,
        TRACK_TYPE_DRUM_BD_ANALOG
    };

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

static uint8_t ui_track_catalog_track_uses_type(uint8_t track,
                                                 track_family_t family,
                                                 track_type_t type,
                                                 const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (track_configs == 0))
    {
        return 0U;
    }

    return (uint8_t)((track_configs[track].family == family) && (track_configs[track].type == type));
}

static uint8_t ui_track_catalog_count_sampler_clip_tracks(uint8_t track,
                                                          const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    uint8_t count = 0U;

    if ((track_configs == 0) || (track >= BRICK_ENTITY_CAPACITY))
    {
        return 0U;
    }

    const uint8_t group_active = (uint8_t)(
        track_configs[BRICK_ENTITY_GROUP_MASTER_ID].type == TRACK_TYPE_GROUP);
    for (uint8_t other_track = 0U; other_track < BRICK_ENTITY_CAPACITY; ++other_track)
    {
        entity_topology_descriptor_t entity;
        if (other_track == track)
        {
            continue;
        }
        if ((entity_topology_resolve(group_active, other_track, &entity) == 0U)
                || (entity.active == 0U))
        {
            continue;
        }

        if (ui_track_catalog_track_uses_type(other_track,
                                             TRACK_FAMILY_SAMPLER,
                                             TRACK_TYPE_STREAM,
                                             track_configs) != 0U)
        {
            ++count;
        }
    }

    return count;
}

bool ui_track_catalog_family_is_engine(track_family_t family)
{
    return (family == TRACK_FAMILY_SYNTH)
            || (family == TRACK_FAMILY_SAMPLER)
            || (family == TRACK_FAMILY_DRUM);
}

bool ui_track_catalog_type_is_valid_for_family(track_family_t family, track_type_t type)
{
    if (((uint8_t)family >= (uint8_t)TRACK_FAMILY_COUNT)
            || ((uint8_t)type >= (uint8_t)TRACK_TYPE_COUNT))
    {
        return false;
    }

    uint8_t type_count = 0U;
    const track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &type_count);
    if ((catalog == 0) || (type_count == 0U))
    {
        return false;
    }

    for (uint8_t i = 0U; i < type_count; ++i)
    {
        if (catalog[i] == type)
        {
            return true;
        }
    }

    return false;
}

bool ui_track_catalog_type_is_available(uint8_t track,
                                        track_family_t family,
                                        track_type_t type,
                                        const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    if ((track >= BRICK_ENTITY_CAPACITY)
            || (track_configs == 0)
            || !ui_track_catalog_type_is_valid_for_family(family, type))
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
    if ((type == TRACK_TYPE_GROUP)
            && (entity.role != ENTITY_ROLE_GROUP_MASTER))
    {
        return false;
    }
    if ((entity.role == ENTITY_ROLE_GROUP_CHILD)
            && (family != TRACK_FAMILY_OFF)
            && !ui_track_catalog_family_is_engine(family))
    {
        return false;
    }

    if (family != TRACK_FAMILY_OFF)
    {
        if ((family == TRACK_FAMILY_SAMPLER) && (type == TRACK_TYPE_STREAM))
        {
            if (ui_track_catalog_track_uses_type(track, family, type, track_configs) != 0U)
            {
                return true;
            }

            return (ui_track_catalog_count_sampler_clip_tracks(track, track_configs) < BRICK6_MAX_CLIP_TRACKS);
        }

        return true;
    }

    return false;
}

bool ui_track_catalog_family_is_available(uint8_t track,
                                          track_family_t family,
                                          const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (track_configs == 0) || ((uint8_t)family >= (uint8_t)TRACK_FAMILY_COUNT))
    {
        return false;
    }

    if (family == TRACK_FAMILY_OFF)
    {
        return true;
    }

    return ui_track_catalog_type_count_for_family(family, track, track_configs) > 0U;
}

track_family_t ui_track_catalog_cfg_family_step(
    track_family_t current,
    int8_t direction,
    uint8_t track,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    uint8_t position = 0U;
    const uint8_t order_count = ui_track_catalog_cfg_family_order_count();

    if ((direction == 0)
            || (order_count == 0U)
            || !ui_track_catalog_cfg_family_order_index(current, &position))
    {
        return current;
    }

    for (uint8_t attempt = 0U; attempt < order_count; ++attempt)
    {
        if (direction > 0)
        {
            if (position + 1U >= order_count)
            {
                return current;
            }

            position = (uint8_t)(position + 1U);
        }
        else
        {
            if (position == 0U)
            {
                return current;
            }

            position = (uint8_t)(position - 1U);
        }

        const track_family_t candidate = ui_track_catalog_cfg_family_order_at(position);
        if (ui_track_catalog_family_is_available(track, candidate, track_configs))
        {
            return candidate;
        }
    }

    return current;
}

bool ui_track_catalog_family_has_available_type(uint8_t track,
                                                track_family_t family,
                                                const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    return ui_track_catalog_family_is_available(track, family, track_configs);
}

uint8_t ui_track_catalog_type_count_for_family(track_family_t family,
                                               uint8_t track,
                                               const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (track_configs == 0) || ((uint8_t)family >= (uint8_t)TRACK_FAMILY_COUNT))
    {
        return 0U;
    }

    if (family == TRACK_FAMILY_OFF)
    {
        return 0U;
    }

    uint8_t catalog_count = 0U;
    const track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &catalog_count);
    if ((catalog == 0) || (catalog_count == 0U))
    {
        return 0U;
    }

    uint8_t count = 0U;
    for (uint8_t i = 0U; i < catalog_count; ++i)
    {
        if (ui_track_catalog_type_is_available(track, family, catalog[i], track_configs))
        {
            ++count;
        }
    }

    return count;
}

uint8_t ui_track_catalog_type_index_for_family(track_family_t family,
                                               track_type_t type,
                                               uint8_t track,
                                               const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    if (!ui_track_catalog_type_is_available(track, family, type, track_configs))
    {
        return 0U;
    }

    uint8_t catalog_count = 0U;
    const track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &catalog_count);
    if ((catalog == 0) || (catalog_count == 0U))
    {
        return 0U;
    }

    uint8_t index = 0U;
    for (uint8_t i = 0U; i < catalog_count; ++i)
    {
        const track_type_t candidate = catalog[i];
        if (!ui_track_catalog_type_is_available(track, family, candidate, track_configs))
        {
            continue;
        }

        if (candidate == type)
        {
            return index;
        }

        ++index;
    }

    return 0U;
}

track_type_t ui_track_catalog_type_from_family_index(track_family_t family,
                                                        uint8_t index,
                                                        uint8_t track,
                                                        const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    if ((track >= BRICK_ENTITY_CAPACITY)
            || (track_configs == 0)
            || ((uint8_t)family >= (uint8_t)TRACK_FAMILY_COUNT))
    {
        return TRACK_TYPE_NONE;
    }

    if (family == TRACK_FAMILY_OFF)
    {
        return TRACK_TYPE_NONE;
    }

    uint8_t catalog_count = 0U;
    const track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &catalog_count);
    if ((catalog == 0) || (catalog_count == 0U))
    {
        return ui_track_catalog_default_type_for_family(family);
    }

    uint8_t current = 0U;
    for (uint8_t i = 0U; i < catalog_count; ++i)
    {
        const track_type_t candidate = catalog[i];
        if (!ui_track_catalog_type_is_available(track, family, candidate, track_configs))
        {
            continue;
        }

        if (current == index)
        {
            return candidate;
        }

        ++current;
    }

    return ui_track_catalog_default_type_for_family(family);
}

track_type_t ui_track_catalog_first_available_type(track_family_t family,
                                                      uint8_t track,
                                                      const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    uint8_t catalog_count = 0U;
    const track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &catalog_count);
    if ((catalog == 0) || (catalog_count == 0U))
    {
        return TRACK_TYPE_NONE;
    }

    for (uint8_t i = 0U; i < catalog_count; ++i)
    {
        if (ui_track_catalog_type_is_available(track, family, catalog[i], track_configs))
        {
            return catalog[i];
        }
    }

    return TRACK_TYPE_NONE;
}

track_type_t ui_track_catalog_default_type_for_family(track_family_t family)
{
    uint8_t catalog_count = 0U;
    const track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &catalog_count);
    if ((catalog != 0) && (catalog_count > 0U))
    {
        return catalog[0];
    }

    return TRACK_TYPE_NONE;
}

const char *ui_track_catalog_family_display_name(track_family_t family)
{
    switch (family)
    {
        case TRACK_FAMILY_OFF:
            return "Off";

        case TRACK_FAMILY_SYNTH:
            return "Synth";
        case TRACK_FAMILY_SAMPLER:
            return "Sampler";
        case TRACK_FAMILY_DRUM:
            return "Drum";
        case TRACK_FAMILY_MIDI:
            return "MIDI";
        case TRACK_FAMILY_EXTERNAL:
            return "External";

        default:
            return "Track";
    }
}

const char *ui_track_catalog_family_short_name(track_family_t family)
{
    switch (family)
    {
        case TRACK_FAMILY_OFF:
            return "Off";

        case TRACK_FAMILY_SYNTH:
            return "Syn";
        case TRACK_FAMILY_SAMPLER:
            return "Smp";
        case TRACK_FAMILY_DRUM:
            return "Drm";
        case TRACK_FAMILY_MIDI:
            return "MID";
        case TRACK_FAMILY_EXTERNAL:
            return "EXT";

        default:
            return "---";
    }
}

const char *ui_track_catalog_type_display_name(track_family_t family, track_type_t type)
{
    if (!ui_track_catalog_type_is_valid_for_family(family, type))
    {
        return "-";
    }

    switch (type)
    {
        case TRACK_TYPE_NONE:
            return "-";

        case TRACK_TYPE_RAM:
            return (family == TRACK_FAMILY_SAMPLER) ? "RAM" : "Sampler";
        case TRACK_TYPE_STREAM:
            return "Stream";
        case TRACK_TYPE_LOOPER:
            return "Looper";
        case TRACK_TYPE_MULTI:
            return "Multi";
        case TRACK_TYPE_GROUP:
            return "Group";
        case TRACK_TYPE_PRISM:
            return "Prism";
        case TRACK_TYPE_WAVE:
            return "Wave";
        case TRACK_TYPE_STACK:
            return "Stack";
        case TRACK_TYPE_FM:
            return "FM";


        case TRACK_TYPE_DRUM_MD:
            return "MD";
        case TRACK_TYPE_DRUM_BD_ANALOG:
            return "BD Analog";
        case TRACK_TYPE_MIDI:
            return "MIDI";
        case TRACK_TYPE_EXTERNAL:
            return "External";

        default:
            return "-";
    }
}

const char *ui_track_catalog_type_short_name(track_family_t family, track_type_t type)
{
    if (!ui_track_catalog_type_is_valid_for_family(family, type))
    {
        return "---";
    }

    switch (type)
    {
        case TRACK_TYPE_NONE:
            return "---";

        case TRACK_TYPE_RAM:
            return (family == TRACK_FAMILY_SAMPLER) ? "RAM" : "Smp";
        case TRACK_TYPE_STREAM:
            return "STRM";
        case TRACK_TYPE_LOOPER:
            return "Loop";
        case TRACK_TYPE_MULTI:
            return "Mult";
        case TRACK_TYPE_GROUP:
            return "GRP";
        case TRACK_TYPE_PRISM:
            return "PRSM";
        case TRACK_TYPE_WAVE:
            return "WAVE";
        case TRACK_TYPE_STACK:
            return "STCK";
        case TRACK_TYPE_FM:
            return "FM";


        case TRACK_TYPE_DRUM_MD:
            return "MD";
        case TRACK_TYPE_DRUM_BD_ANALOG:
            return "BDA";
        case TRACK_TYPE_MIDI:
            return "MID";
        case TRACK_TYPE_EXTERNAL:
            return "EXT";

        default:
            return "---";
    }
}
