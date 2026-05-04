#include "Core/track_state.h"

#include <string.h>

#include "UI/ui_track_catalog.h"

static ui_track_config_t g_track_configs[UI_TRACK_COUNT];
static uint8_t g_track_midi_channel[UI_TRACK_COUNT];
static ui_track_midi_source_t g_track_midi_source[UI_TRACK_COUNT];
static track_voice_group_role_t g_track_voice_group_role[UI_TRACK_COUNT];
static uint32_t g_track_revision[UI_TRACK_COUNT];
static uint32_t g_track_state_global_revision = 0U;

static ui_track_config_t track_state_default_config(void)
{
    ui_track_config_t config = {
        .family = UI_TRACK_FAMILY_OFF,
        .type = UI_TRACK_TYPE_AUDIO,
    };

    return config;
}

static void track_state_normalize_config(ui_track_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    if ((config->family == UI_TRACK_FAMILY_SYNTH)
            && (config->type == UI_TRACK_TYPE_SAMPLER))
    {
        config->family = UI_TRACK_FAMILY_SAMPLER;
        config->type = UI_TRACK_TYPE_ONE_SHOT;
        return;
    }

    if ((config->family == UI_TRACK_FAMILY_SAMPLER)
            && (config->type == UI_TRACK_TYPE_SAMPLER))
    {
        config->type = UI_TRACK_TYPE_ONE_SHOT;
        return;
    }

    if ((config->family == UI_TRACK_FAMILY_SAMPLER)
            && (config->type == UI_TRACK_TYPE_SLICER))
    {
        return;
    }
}

static void track_state_bump_revision(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return;
    }

    ++g_track_state_global_revision;
    ++g_track_revision[track];
}

static uint8_t track_state_voice_group_roles_are_valid(const track_voice_group_role_t roles[UI_TRACK_COUNT])
{
    if (roles == NULL)
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const track_voice_group_role_t role = roles[track];
        if ((uint8_t)role >= (uint8_t)TRACK_VOICE_GROUP_ROLE_COUNT)
        {
            return 0U;
        }

        if (role != TRACK_VOICE_GROUP_ROLE_SLAVE)
        {
            continue;
        }

        if (track == 0U)
        {
            return 0U;
        }

        const track_voice_group_role_t left = roles[(uint8_t)(track - 1U)];
        if ((left != TRACK_VOICE_GROUP_ROLE_MASTER) && (left != TRACK_VOICE_GROUP_ROLE_SLAVE))
        {
            return 0U;
        }
    }

    return 1U;
}

static void track_state_commit_entry(uint8_t track,
                                     const ui_track_config_t *next_config,
                                     uint8_t next_midi_channel,
                                     ui_track_midi_source_t next_midi_source)
{
    if ((track >= UI_TRACK_COUNT) || (next_config == NULL))
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
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        g_track_configs[track] = track_state_default_config();
        g_track_midi_channel[track] = (uint8_t)((track < 16U) ? (track + 1U) : 16U);
        g_track_midi_source[track] = UI_TRACK_MIDI_SRC_ALL;
        g_track_voice_group_role[track] = TRACK_VOICE_GROUP_ROLE_SOLO;
        g_track_revision[track] = 0U;
    }

    g_track_state_global_revision = 1U;
}

const ui_track_config_t *track_state_get_configs(void)
{
    return &g_track_configs[0];
}

ui_track_config_t track_state_get_config(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return track_state_default_config();
    }

    return g_track_configs[track];
}

ui_track_family_t track_state_get_family(uint8_t track)
{
    return track_state_get_config(track).family;
}

ui_track_type_t track_state_get_type(uint8_t track)
{
    return track_state_get_config(track).type;
}

uint8_t track_state_get_midi_channel(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 1U;
    }

    const uint8_t channel = g_track_midi_channel[track];
    return (channel < 1U) ? 1U : ((channel > 16U) ? 16U : channel);
}

ui_track_midi_source_t track_state_get_midi_source(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return UI_TRACK_MIDI_SRC_ALL;
    }

    const uint8_t source = (uint8_t)g_track_midi_source[track];
    if (source >= (uint8_t)UI_TRACK_MIDI_SRC_COUNT)
    {
        return UI_TRACK_MIDI_SRC_ALL;
    }

    return (ui_track_midi_source_t)source;
}

track_voice_group_role_t track_state_get_voice_group_role(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return TRACK_VOICE_GROUP_ROLE_SOLO;
    }

    const track_voice_group_role_t role = g_track_voice_group_role[track];
    if ((uint8_t)role >= (uint8_t)TRACK_VOICE_GROUP_ROLE_COUNT)
    {
        return TRACK_VOICE_GROUP_ROLE_SOLO;
    }

    return role;
}

bool track_state_set_voice_group_role(uint8_t track, track_voice_group_role_t role)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)role >= (uint8_t)TRACK_VOICE_GROUP_ROLE_COUNT))
    {
        return false;
    }

    if (g_track_voice_group_role[track] == role)
    {
        return true;
    }

    track_voice_group_role_t next_roles[UI_TRACK_COUNT];
    memcpy(next_roles, g_track_voice_group_role, sizeof(next_roles));
    next_roles[track] = role;

    if (track_state_voice_group_roles_are_valid(next_roles) == 0U)
    {
        return false;
    }

    g_track_voice_group_role[track] = role;
    track_state_bump_revision(track);
    return true;
}

bool track_state_is_voice_group_role_solo(uint8_t track)
{
    return (track_state_get_voice_group_role(track) == TRACK_VOICE_GROUP_ROLE_SOLO);
}

bool track_state_is_voice_group_role_master(uint8_t track)
{
    return (track_state_get_voice_group_role(track) == TRACK_VOICE_GROUP_ROLE_MASTER);
}

bool track_state_is_voice_group_role_slave(uint8_t track)
{
    return (track_state_get_voice_group_role(track) == TRACK_VOICE_GROUP_ROLE_SLAVE);
}

bool track_state_set_track_family(uint8_t track, ui_track_family_t family)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT))
    {
        return false;
    }

    ui_track_config_t next_configs[UI_TRACK_COUNT];
    memcpy(next_configs, track_state_get_configs(), sizeof(next_configs));

    ui_track_config_t next_config = next_configs[track];
    next_config.family = family;

    if (family == UI_TRACK_FAMILY_OFF)
    {
        next_config.type = UI_TRACK_TYPE_AUDIO;
    }
    else if (!ui_track_catalog_type_is_available(track, family, next_config.type, next_configs))
    {
        next_config.type = ui_track_catalog_first_available_type(family, track, next_configs);
    }

    next_configs[track] = next_config;

    if ((family != UI_TRACK_FAMILY_OFF)
            && !ui_track_catalog_family_is_available(track, family, next_configs))
    {
        return false;
    }

    if ((family != UI_TRACK_FAMILY_OFF)
            && !ui_track_catalog_type_is_available(track, family, next_config.type, next_configs))
    {
        return false;
    }

    track_state_commit_entry(track,
                             &next_config,
                             track_state_get_midi_channel(track),
                             track_state_get_midi_source(track));
    return true;
}

bool track_state_set_track_type(uint8_t track, ui_track_type_t type)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)type >= (uint8_t)UI_TRACK_TYPE_COUNT))
    {
        return false;
    }

    const ui_track_family_t family = track_state_get_family(track);
    if (!ui_track_catalog_type_is_valid_for_family(family, type))
    {
        return false;
    }

    ui_track_config_t next_configs[UI_TRACK_COUNT];
    memcpy(next_configs, track_state_get_configs(), sizeof(next_configs));

    if (!ui_track_catalog_type_is_available(track, family, type, next_configs))
    {
        return false;
    }

    ui_track_config_t next_config = next_configs[track];
    next_config.type = type;
    track_state_normalize_config(&next_config);
    if (family == UI_TRACK_FAMILY_OFF)
    {
        next_config.type = UI_TRACK_TYPE_AUDIO;
    }

    track_state_commit_entry(track,
                             &next_config,
                             track_state_get_midi_channel(track),
                             track_state_get_midi_source(track));
    return true;
}

bool track_state_set_track_midi_channel(uint8_t track, uint8_t channel_1_16)
{
    if ((track >= UI_TRACK_COUNT) || (channel_1_16 < 1U) || (channel_1_16 > 16U))
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

bool track_state_set_track_midi_source(uint8_t track, ui_track_midi_source_t source)
{
    if ((track >= UI_TRACK_COUNT) || ((uint8_t)source >= (uint8_t)UI_TRACK_MIDI_SRC_COUNT))
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

bool track_state_apply_bulk(const uint8_t family[UI_TRACK_COUNT],
                           const uint8_t type[UI_TRACK_COUNT],
                           const uint8_t midi_channel[UI_TRACK_COUNT],
                           const uint8_t midi_source[UI_TRACK_COUNT])
{
    if ((family == NULL) || (type == NULL) || (midi_channel == NULL) || (midi_source == NULL))
    {
        return false;
    }

    ui_track_config_t next_configs[UI_TRACK_COUNT];
    uint8_t next_channels[UI_TRACK_COUNT];
    ui_track_midi_source_t next_sources[UI_TRACK_COUNT];

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        next_configs[track] = track_state_get_config(track);
        next_channels[track] = track_state_get_midi_channel(track);
        next_sources[track] = track_state_get_midi_source(track);
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_family_t fam = (ui_track_family_t)family[track];
        ui_track_type_t typ = (ui_track_type_t)type[track];
        const ui_track_midi_source_t src = (ui_track_midi_source_t)midi_source[track];

        if (((uint8_t)fam >= (uint8_t)UI_TRACK_FAMILY_COUNT)
                || ((uint8_t)src >= (uint8_t)UI_TRACK_MIDI_SRC_COUNT))
        {
            return false;
        }

        if (fam == UI_TRACK_FAMILY_OFF)
        {
            typ = UI_TRACK_TYPE_AUDIO;
        }
        else
        {
            ui_track_config_t normalized = {
                .family = fam,
                .type = typ
            };
            track_state_normalize_config(&normalized);
            if (!ui_track_catalog_type_is_valid_for_family(normalized.family, normalized.type))
            {
                return false;
            }

            next_configs[track].family = normalized.family;
            next_configs[track].type = normalized.type;
        }

        if (fam == UI_TRACK_FAMILY_OFF)
        {
            next_configs[track].family = fam;
            next_configs[track].type = typ;
        }
        next_channels[track] = (midi_channel[track] < 1U)
            ? 1U
            : ((midi_channel[track] > 16U) ? 16U : midi_channel[track]);
        next_sources[track] = src;
    }

    uint8_t input_family_count[(uint8_t)UI_TRACK_FAMILY_COUNT] = { 0U };
    uint8_t master_family_count = 0U;

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_family_t fam = next_configs[track].family;
        if (ui_track_catalog_family_is_input(fam))
        {
            input_family_count[(uint8_t)fam]++;
            if (input_family_count[(uint8_t)fam] > 1U)
            {
                return false;
            }
        }

        if (fam == UI_TRACK_FAMILY_MASTER)
        {
            master_family_count++;
            if (master_family_count > 1U)
            {
                return false;
            }
        }
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_family_t fam = next_configs[track].family;
        const ui_track_type_t typ = next_configs[track].type;

        if ((fam != UI_TRACK_FAMILY_OFF)
                && !ui_track_catalog_family_is_available(track, fam, next_configs))
        {
            return false;
        }

        if ((fam != UI_TRACK_FAMILY_OFF)
                && !ui_track_catalog_type_is_available(track, fam, typ, next_configs))
        {
            return false;
        }
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        track_state_commit_entry(track, &next_configs[track], next_channels[track], next_sources[track]);
    }

    return true;
}

bool track_state_apply_voice_group_roles_bulk(const uint8_t role[UI_TRACK_COUNT])
{
    if (role == NULL)
    {
        return false;
    }

    track_voice_group_role_t next_roles[UI_TRACK_COUNT];
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const uint8_t role_u8 = role[track];
        if (role_u8 >= (uint8_t)TRACK_VOICE_GROUP_ROLE_COUNT)
        {
            return false;
        }
        next_roles[track] = (track_voice_group_role_t)role_u8;
    }

    if (track_state_voice_group_roles_are_valid(next_roles) == 0U)
    {
        return false;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if (g_track_voice_group_role[track] != next_roles[track])
        {
            g_track_voice_group_role[track] = next_roles[track];
            track_state_bump_revision(track);
        }
    }

    return true;
}

uint8_t track_state_count_tracks_with_family(ui_track_family_t family)
{
    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
    {
        return 0U;
    }

    uint8_t count = 0U;
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if (g_track_configs[track].family == family)
        {
            ++count;
        }
    }

    return count;
}

uint32_t track_state_get_revision(uint8_t track)
{
    if (track >= UI_TRACK_COUNT)
    {
        return 0U;
    }

    return g_track_revision[track];
}

uint32_t track_state_get_global_revision(void)
{
    return g_track_state_global_revision;
}
