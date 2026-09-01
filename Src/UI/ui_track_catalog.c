#include "ui_track_catalog.h"

#include "Track/track_catalog.h"

static const track_family_t g_cfg_play_family_order[] = {
    TRACK_FAMILY_OFF, TRACK_FAMILY_SYNTH, TRACK_FAMILY_DRUM,
    TRACK_FAMILY_MIDI, TRACK_FAMILY_EXTERNAL, TRACK_FAMILY_SAMPLER
};

uint8_t ui_track_catalog_cfg_family_order_count(void)
{
    return (uint8_t)(sizeof(g_cfg_play_family_order) / sizeof(g_cfg_play_family_order[0]));
}

track_family_t ui_track_catalog_cfg_family_order_at(uint8_t index)
{
    return (index < ui_track_catalog_cfg_family_order_count())
        ? g_cfg_play_family_order[index] : TRACK_FAMILY_OFF;
}

bool ui_track_catalog_cfg_family_order_index(track_family_t family, uint8_t *out_index)
{
    if (out_index == 0) return false;
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

bool ui_track_catalog_family_is_engine(track_family_t family)
{
    return track_catalog_family_is_engine(family);
}

bool ui_track_catalog_type_is_valid_for_family(track_family_t family, track_type_t type)
{
    return track_catalog_type_is_valid_for_family(family, type);
}

bool ui_track_catalog_type_is_available(
    uint8_t track, track_family_t family, track_type_t type,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    return track_catalog_type_is_available(track, family, type, track_configs);
}

bool ui_track_catalog_family_is_available(
    uint8_t track, track_family_t family,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    return track_catalog_family_is_available(track, family, track_configs);
}

track_family_t ui_track_catalog_cfg_family_step(
    track_family_t current, int8_t direction, uint8_t track,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    uint8_t position = 0U;
    const uint8_t order_count = ui_track_catalog_cfg_family_order_count();
    if ((direction == 0) || (order_count == 0U)
            || !ui_track_catalog_cfg_family_order_index(current, &position))
        return current;

    for (uint8_t attempt = 0U; attempt < order_count; ++attempt)
    {
        if (direction > 0)
        {
            if (position + 1U >= order_count) return current;
            ++position;
        }
        else
        {
            if (position == 0U) return current;
            --position;
        }
        const track_family_t candidate = ui_track_catalog_cfg_family_order_at(position);
        if (ui_track_catalog_family_is_available(track, candidate, track_configs))
            return candidate;
    }
    return current;
}

bool ui_track_catalog_family_has_available_type(
    uint8_t track, track_family_t family,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    return ui_track_catalog_family_is_available(track, family, track_configs);
}

uint8_t ui_track_catalog_type_count_for_family(
    track_family_t family, uint8_t track,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    return track_catalog_type_count_for_family(family, track, track_configs);
}

uint8_t ui_track_catalog_type_index_for_family(
    track_family_t family, track_type_t type, uint8_t track,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    if (!track_catalog_type_is_available(track, family, type, track_configs)) return 0U;
    uint8_t index = 0U;
    for (uint8_t i = 0U; ; ++i)
    {
        const track_type_t candidate = track_catalog_type_at(family, i);
        if (candidate == TRACK_TYPE_NONE) return 0U;
        if (!track_catalog_type_is_available(track, family, candidate, track_configs)) continue;
        if (candidate == type) return index;
        ++index;
    }
}

track_type_t ui_track_catalog_type_from_family_index(
    track_family_t family, uint8_t index, uint8_t track,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    uint8_t available_index = 0U;
    for (uint8_t i = 0U; ; ++i)
    {
        const track_type_t candidate = track_catalog_type_at(family, i);
        if (candidate == TRACK_TYPE_NONE) return track_catalog_default_type_for_family(family);
        if (!track_catalog_type_is_available(track, family, candidate, track_configs)) continue;
        if (available_index == index) return candidate;
        ++available_index;
    }
}

track_type_t ui_track_catalog_first_available_type(
    track_family_t family, uint8_t track,
    const track_config_t track_configs[BRICK_ENTITY_CAPACITY])
{
    return track_catalog_first_available_type(family, track, track_configs);
}

track_type_t ui_track_catalog_default_type_for_family(track_family_t family)
{
    return track_catalog_default_type_for_family(family);
}

const char *ui_track_catalog_family_display_name(track_family_t family)
{
    switch (family)
    {
        case TRACK_FAMILY_OFF: return "Off";
        case TRACK_FAMILY_SYNTH: return "Synth";
        case TRACK_FAMILY_SAMPLER: return "Sampler";
        case TRACK_FAMILY_DRUM: return "Drum";
        case TRACK_FAMILY_MIDI: return "MIDI";
        case TRACK_FAMILY_EXTERNAL: return "External";
        default: return "Track";
    }
}

const char *ui_track_catalog_family_short_name(track_family_t family)
{
    return track_catalog_family_short_name(family);
}

const char *ui_track_catalog_type_display_name(track_family_t family, track_type_t type)
{
    if (!track_catalog_type_is_valid_for_family(family, type)) return "-";
    switch (type)
    {
        case TRACK_TYPE_NONE: return "-";
        case TRACK_TYPE_RAM: return (family == TRACK_FAMILY_SAMPLER) ? "RAM" : "Sampler";
        case TRACK_TYPE_STREAM: return "Stream";
        case TRACK_TYPE_LOOPER: return "Looper";
        case TRACK_TYPE_MULTI: return "Multi";
        case TRACK_TYPE_GROUP: return "Group";
        case TRACK_TYPE_PRISM: return "Prism";
        case TRACK_TYPE_WAVE: return "Wave";
        case TRACK_TYPE_STACK: return "Stack";
        case TRACK_TYPE_FM: return "FM";
        case TRACK_TYPE_DRUM_MD: return "MD";
        case TRACK_TYPE_DRUM_BD_ANALOG: return "BD Analog";
        case TRACK_TYPE_MIDI: return "MIDI";
        case TRACK_TYPE_EXTERNAL: return "External";
        default: return "-";
    }
}

const char *ui_track_catalog_type_short_name(track_family_t family, track_type_t type)
{
    if (!track_catalog_type_is_valid_for_family(family, type)) return "---";
    switch (type)
    {
        case TRACK_TYPE_NONE: return "---";
        case TRACK_TYPE_RAM: return (family == TRACK_FAMILY_SAMPLER) ? "RAM" : "Smp";
        case TRACK_TYPE_STREAM: return "STRM";
        case TRACK_TYPE_LOOPER: return "Loop";
        case TRACK_TYPE_MULTI: return "Mult";
        case TRACK_TYPE_GROUP: return "GRP";
        case TRACK_TYPE_PRISM: return "PRSM";
        case TRACK_TYPE_WAVE: return "WAVE";
        case TRACK_TYPE_STACK: return "STCK";
        case TRACK_TYPE_FM: return "FM";
        case TRACK_TYPE_DRUM_MD: return "MD";
        case TRACK_TYPE_DRUM_BD_ANALOG: return "BDA";
        case TRACK_TYPE_MIDI: return "MID";
        case TRACK_TYPE_EXTERNAL: return "EXT";
        default: return "---";
    }
}
