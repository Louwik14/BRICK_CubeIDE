#include "ui_track_catalog.h"

static const ui_track_family_t g_cfg_play_family_order[] = {
    UI_TRACK_FAMILY_OFF,
    UI_TRACK_FAMILY_SYNTH,
    UI_TRACK_FAMILY_DRUM,
    UI_TRACK_FAMILY_MIDI,
    UI_TRACK_FAMILY_EXTERNAL,
    UI_TRACK_FAMILY_SAMPLER,
};

uint8_t ui_track_catalog_cfg_family_order_count(void)
{
    return (uint8_t)(sizeof(g_cfg_play_family_order) / sizeof(g_cfg_play_family_order[0]));
}

ui_track_family_t ui_track_catalog_cfg_family_order_at(uint8_t index)
{
    if (index >= ui_track_catalog_cfg_family_order_count())
    {
        return UI_TRACK_FAMILY_OFF;
    }

    return g_cfg_play_family_order[index];
}

bool ui_track_catalog_cfg_family_order_index(ui_track_family_t family, uint8_t *out_index)
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

static const ui_track_type_t *ui_track_catalog_get_types_for_family(ui_track_family_t family, uint8_t *out_count)
{
    static const ui_track_type_t k_synth_types[] = {
        UI_TRACK_TYPE_PRISM,
        UI_TRACK_TYPE_WAVE,
        UI_TRACK_TYPE_STACK,
    };
    static const ui_track_type_t k_sampler_types[] = {
        UI_TRACK_TYPE_RAM,
        UI_TRACK_TYPE_STREAM,
        UI_TRACK_TYPE_LOOPER,
        UI_TRACK_TYPE_MULTI,
        UI_TRACK_TYPE_GROUP
    };
    static const ui_track_type_t k_midi_types[] = { UI_TRACK_TYPE_MIDI };
    static const ui_track_type_t k_external_types[] = { UI_TRACK_TYPE_EXTERNAL };
    static const ui_track_type_t k_drum_types[] = {
        UI_TRACK_TYPE_DRUM_MD,
        UI_TRACK_TYPE_DRUM_BD_ANALOG
    };

    if (out_count == 0)
    {
        return 0;
    }

    switch (family)
    {
        case UI_TRACK_FAMILY_SYNTH:
            *out_count = (uint8_t)(sizeof(k_synth_types) / sizeof(k_synth_types[0]));
            return k_synth_types;

        case UI_TRACK_FAMILY_SAMPLER:
            *out_count = (uint8_t)(sizeof(k_sampler_types) / sizeof(k_sampler_types[0]));
            return k_sampler_types;

        case UI_TRACK_FAMILY_DRUM:
            *out_count = (uint8_t)(sizeof(k_drum_types) / sizeof(k_drum_types[0]));
            return k_drum_types;

        case UI_TRACK_FAMILY_MIDI:
            *out_count = (uint8_t)(sizeof(k_midi_types) / sizeof(k_midi_types[0]));
            return k_midi_types;

        case UI_TRACK_FAMILY_EXTERNAL:
            *out_count = (uint8_t)(sizeof(k_external_types) / sizeof(k_external_types[0]));
            return k_external_types;

        case UI_TRACK_FAMILY_OFF:
        default:
            *out_count = 0U;
            return 0;
    }
}

static uint8_t ui_track_catalog_track_uses_type(uint8_t track,
                                                 ui_track_family_t family,
                                                 ui_track_type_t type,
                                                 const ui_track_config_t track_configs[UI_TRACK_COUNT])
{
    if ((track >= UI_TRACK_COUNT) || (track_configs == 0))
    {
        return 0U;
    }

    return (uint8_t)((track_configs[track].family == family) && (track_configs[track].type == type));
}

static uint8_t ui_track_catalog_count_sampler_clip_tracks(uint8_t track,
                                                          const ui_track_config_t track_configs[UI_TRACK_COUNT])
{
    uint8_t count = 0U;

    if ((track_configs == 0) || (track >= UI_TRACK_COUNT))
    {
        return 0U;
    }

    for (uint8_t other_track = 0U; other_track < UI_TRACK_COUNT; ++other_track)
    {
        if (other_track == track)
        {
            continue;
        }

        if (ui_track_catalog_track_uses_type(other_track,
                                             UI_TRACK_FAMILY_SAMPLER,
                                             UI_TRACK_TYPE_STREAM,
                                             track_configs) != 0U)
        {
            ++count;
        }
    }

    return count;
}

bool ui_track_catalog_family_is_engine(ui_track_family_t family)
{
    return (family == UI_TRACK_FAMILY_SYNTH)
            || (family == UI_TRACK_FAMILY_SAMPLER)
            || (family == UI_TRACK_FAMILY_DRUM);
}

bool ui_track_catalog_type_is_valid_for_family(ui_track_family_t family, ui_track_type_t type)
{
    if (((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
            || ((uint8_t)type >= (uint8_t)UI_TRACK_TYPE_COUNT))
    {
        return false;
    }

    uint8_t type_count = 0U;
    const ui_track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &type_count);
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
                                        ui_track_family_t family,
                                        ui_track_type_t type,
                                        const ui_track_config_t track_configs[UI_TRACK_COUNT])
{
    if ((track >= UI_TRACK_COUNT)
            || (track_configs == 0)
            || !ui_track_catalog_type_is_valid_for_family(family, type))
    {
        return false;
    }

    if ((family == UI_TRACK_FAMILY_SAMPLER)
            && (type == UI_TRACK_TYPE_GROUP)
            && (track != TRACK_TOPOLOGY_GROUP_PARENT_TRACK))
    {
        return false;
    }

    if (family != UI_TRACK_FAMILY_OFF)
    {
        if ((family == UI_TRACK_FAMILY_SAMPLER) && (type == UI_TRACK_TYPE_STREAM))
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
                                          ui_track_family_t family,
                                          const ui_track_config_t track_configs[UI_TRACK_COUNT])
{
    if ((track >= UI_TRACK_COUNT) || (track_configs == 0) || ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT))
    {
        return false;
    }

    if (family == UI_TRACK_FAMILY_OFF)
    {
        return true;
    }

    return ui_track_catalog_type_count_for_family(family, track, track_configs) > 0U;
}

ui_track_family_t ui_track_catalog_cfg_family_step(
    ui_track_family_t current,
    int8_t direction,
    uint8_t track,
    const ui_track_config_t track_configs[UI_TRACK_COUNT])
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

        const ui_track_family_t candidate = ui_track_catalog_cfg_family_order_at(position);
        if (ui_track_catalog_family_is_available(track, candidate, track_configs))
        {
            return candidate;
        }
    }

    return current;
}

bool ui_track_catalog_family_has_available_type(uint8_t track,
                                                ui_track_family_t family,
                                                const ui_track_config_t track_configs[UI_TRACK_COUNT])
{
    return ui_track_catalog_family_is_available(track, family, track_configs);
}

uint8_t ui_track_catalog_type_count_for_family(ui_track_family_t family,
                                               uint8_t track,
                                               const ui_track_config_t track_configs[UI_TRACK_COUNT])
{
    if ((track >= UI_TRACK_COUNT) || (track_configs == 0) || ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT))
    {
        return 0U;
    }

    if (family == UI_TRACK_FAMILY_OFF)
    {
        return 0U;
    }

    uint8_t catalog_count = 0U;
    const ui_track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &catalog_count);
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

uint8_t ui_track_catalog_type_index_for_family(ui_track_family_t family,
                                               ui_track_type_t type,
                                               uint8_t track,
                                               const ui_track_config_t track_configs[UI_TRACK_COUNT])
{
    if (!ui_track_catalog_type_is_available(track, family, type, track_configs))
    {
        return 0U;
    }

    uint8_t catalog_count = 0U;
    const ui_track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &catalog_count);
    if ((catalog == 0) || (catalog_count == 0U))
    {
        return 0U;
    }

    uint8_t index = 0U;
    for (uint8_t i = 0U; i < catalog_count; ++i)
    {
        const ui_track_type_t candidate = catalog[i];
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

ui_track_type_t ui_track_catalog_type_from_family_index(ui_track_family_t family,
                                                        uint8_t index,
                                                        uint8_t track,
                                                        const ui_track_config_t track_configs[UI_TRACK_COUNT])
{
    if ((track >= UI_TRACK_COUNT)
            || (track_configs == 0)
            || ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT))
    {
        return UI_TRACK_TYPE_NONE;
    }

    if (family == UI_TRACK_FAMILY_OFF)
    {
        return UI_TRACK_TYPE_NONE;
    }

    uint8_t catalog_count = 0U;
    const ui_track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &catalog_count);
    if ((catalog == 0) || (catalog_count == 0U))
    {
        return ui_track_catalog_default_type_for_family(family);
    }

    uint8_t current = 0U;
    for (uint8_t i = 0U; i < catalog_count; ++i)
    {
        const ui_track_type_t candidate = catalog[i];
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

ui_track_type_t ui_track_catalog_first_available_type(ui_track_family_t family,
                                                      uint8_t track,
                                                      const ui_track_config_t track_configs[UI_TRACK_COUNT])
{
    uint8_t catalog_count = 0U;
    const ui_track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &catalog_count);
    if ((catalog == 0) || (catalog_count == 0U))
    {
        return UI_TRACK_TYPE_NONE;
    }

    for (uint8_t i = 0U; i < catalog_count; ++i)
    {
        if (ui_track_catalog_type_is_available(track, family, catalog[i], track_configs))
        {
            return catalog[i];
        }
    }

    return UI_TRACK_TYPE_NONE;
}

ui_track_type_t ui_track_catalog_default_type_for_family(ui_track_family_t family)
{
    uint8_t catalog_count = 0U;
    const ui_track_type_t *const catalog = ui_track_catalog_get_types_for_family(family, &catalog_count);
    if ((catalog != 0) && (catalog_count > 0U))
    {
        return catalog[0];
    }

    return UI_TRACK_TYPE_NONE;
}

const char *ui_track_catalog_family_display_name(ui_track_family_t family)
{
    switch (family)
    {
        case UI_TRACK_FAMILY_OFF:
            return "Off";

        case UI_TRACK_FAMILY_SYNTH:
            return "Synth";
        case UI_TRACK_FAMILY_SAMPLER:
            return "Sampler";
        case UI_TRACK_FAMILY_DRUM:
            return "Drum";
        case UI_TRACK_FAMILY_MIDI:
            return "MIDI";
        case UI_TRACK_FAMILY_EXTERNAL:
            return "External";

        default:
            return "Track";
    }
}

const char *ui_track_catalog_family_short_name(ui_track_family_t family)
{
    switch (family)
    {
        case UI_TRACK_FAMILY_OFF:
            return "Off";

        case UI_TRACK_FAMILY_SYNTH:
            return "Syn";
        case UI_TRACK_FAMILY_SAMPLER:
            return "Smp";
        case UI_TRACK_FAMILY_DRUM:
            return "Drm";
        case UI_TRACK_FAMILY_MIDI:
            return "MID";
        case UI_TRACK_FAMILY_EXTERNAL:
            return "EXT";

        default:
            return "---";
    }
}

const char *ui_track_catalog_type_display_name(ui_track_family_t family, ui_track_type_t type)
{
    if (!ui_track_catalog_type_is_valid_for_family(family, type))
    {
        return "-";
    }

    switch (type)
    {
        case UI_TRACK_TYPE_NONE:
            return "-";

        case UI_TRACK_TYPE_RAM:
            return (family == UI_TRACK_FAMILY_SAMPLER) ? "RAM" : "Sampler";
        case UI_TRACK_TYPE_STREAM:
            return "Stream";
        case UI_TRACK_TYPE_LOOPER:
            return "Looper";
        case UI_TRACK_TYPE_MULTI:
            return "Multi";
        case UI_TRACK_TYPE_GROUP:
            return "Group";
        case UI_TRACK_TYPE_PRISM:
            return "Prism";
        case UI_TRACK_TYPE_WAVE:
            return "Wave";
        case UI_TRACK_TYPE_STACK:
            return "Stack";


        case UI_TRACK_TYPE_DRUM_MD:
            return "MD";
        case UI_TRACK_TYPE_DRUM_BD_ANALOG:
            return "BD Analog";
        case UI_TRACK_TYPE_MIDI:
            return "MIDI";
        case UI_TRACK_TYPE_EXTERNAL:
            return "External";

        default:
            return "-";
    }
}

const char *ui_track_catalog_type_short_name(ui_track_family_t family, ui_track_type_t type)
{
    if (!ui_track_catalog_type_is_valid_for_family(family, type))
    {
        return "---";
    }

    switch (type)
    {
        case UI_TRACK_TYPE_NONE:
            return "---";

        case UI_TRACK_TYPE_RAM:
            return (family == UI_TRACK_FAMILY_SAMPLER) ? "RAM" : "Smp";
        case UI_TRACK_TYPE_STREAM:
            return "STRM";
        case UI_TRACK_TYPE_LOOPER:
            return "Loop";
        case UI_TRACK_TYPE_MULTI:
            return "Mult";
        case UI_TRACK_TYPE_GROUP:
            return "GRP";
        case UI_TRACK_TYPE_PRISM:
            return "PRSM";
        case UI_TRACK_TYPE_WAVE:
            return "WAVE";
        case UI_TRACK_TYPE_STACK:
            return "STCK";


        case UI_TRACK_TYPE_DRUM_MD:
            return "MD";
        case UI_TRACK_TYPE_DRUM_BD_ANALOG:
            return "BDA";
        case UI_TRACK_TYPE_MIDI:
            return "MID";
        case UI_TRACK_TYPE_EXTERNAL:
            return "EXT";

        default:
            return "---";
    }
}
