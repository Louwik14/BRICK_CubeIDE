#include "Core/track_state.h"

#include <string.h>

#include "Core/audio_modulation_projection.h"
#include "Core/track_input_ownership.h"
#include "Mod/mod_matrix.h"
#include "UI/ui_track_catalog.h"

static ui_track_config_t g_track_configs[TRACK_CONFIG_CAPACITY];
static uint8_t g_track_midi_channel[TRACK_CONFIG_CAPACITY];
static ui_track_midi_source_t g_track_midi_source[TRACK_CONFIG_CAPACITY];
static uint32_t g_track_revision[TRACK_CONFIG_CAPACITY];
static uint32_t g_track_state_global_revision = 0U;

static ui_track_config_t track_state_default_config(void)
{
    ui_track_config_t config = {
        .family = UI_TRACK_FAMILY_OFF,
        .type = UI_TRACK_TYPE_NONE,
    };

    return config;
}

static ui_track_config_t track_state_initial_config(uint8_t track)
{
    entity_topology_descriptor_t entity;
    if ((entity_topology_resolve(1U, (brick_entity_id_t)track, &entity) != 0U)
            && (entity.role == ENTITY_ROLE_GROUP_CHILD))
    {
        return (ui_track_config_t){
            .family = UI_TRACK_FAMILY_SAMPLER,
            .type = UI_TRACK_TYPE_RAM,
        };
    }
    return track_state_default_config();
}

static void track_state_normalize_config(ui_track_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    if ((config->family == UI_TRACK_FAMILY_SAMPLER)
            && ((config->type == UI_TRACK_TYPE_RAM)
                || (config->type == UI_TRACK_TYPE_STREAM)
                || (config->type == UI_TRACK_TYPE_LOOPER)
                || (config->type == UI_TRACK_TYPE_MULTI)
                || (config->type == UI_TRACK_TYPE_GROUP)))
    {
        return;
    }

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
                                     const ui_track_config_t *next_config,
                                     uint8_t next_midi_channel,
                                     ui_track_midi_source_t next_midi_source)
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
        g_track_midi_source[track] = UI_TRACK_MIDI_SRC_ALL;
        g_track_revision[track] = 0U;
    }

    g_track_state_global_revision = 1U;
    track_input_ownership_init(g_track_configs);
    audio_modulation_projection_init();
    audio_modulation_projection_publish();
}

const ui_track_config_t *track_state_get_configs(void)
{
    return &g_track_configs[0];
}

ui_track_config_t track_state_get_config(uint8_t track)
{
    if (track >= TRACK_CONFIG_CAPACITY)
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
    if (track >= TRACK_CONFIG_CAPACITY)
    {
        return 1U;
    }

    const uint8_t channel = g_track_midi_channel[track];
    return (channel < 1U) ? 1U : ((channel > 16U) ? 16U : channel);
}

ui_track_midi_source_t track_state_get_midi_source(uint8_t track)
{
    if (track >= TRACK_CONFIG_CAPACITY)
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

bool track_state_set_track_family(uint8_t track, ui_track_family_t family)
{
    if ((track >= TRACK_CONFIG_CAPACITY) || ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT))
    {
        return false;
    }

    if (entity_topology_is_active(track) == 0U)
    {
        return false;
    }

    ui_track_config_t next_configs[TRACK_CONFIG_CAPACITY];
    memcpy(next_configs, track_state_get_configs(), sizeof(next_configs));

    ui_track_config_t next_config = next_configs[track];
    next_config.family = family;

    if (family == UI_TRACK_FAMILY_OFF)
    {
        next_config.type = UI_TRACK_TYPE_NONE;
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

    if (track_input_ownership_apply_configs(next_configs) == 0U)
    {
        return false;
    }

    track_state_commit_entry(track,
                             &next_config,
                             track_state_get_midi_channel(track),
                             track_state_get_midi_source(track));
    audio_modulation_projection_publish();
    mod_matrix_publish_control_snapshot_track(track);
    return true;
}

bool track_state_set_track_type(uint8_t track, ui_track_type_t type)
{
    if ((track >= TRACK_CONFIG_CAPACITY) || ((uint8_t)type >= (uint8_t)UI_TRACK_TYPE_COUNT))
    {
        return false;
    }


    if (entity_topology_is_active(track) == 0U)
    {
        return false;
    }

    const ui_track_family_t family = track_state_get_family(track);
    if (!ui_track_catalog_type_is_valid_for_family(family, type))
    {
        return false;
    }

    ui_track_config_t next_configs[TRACK_CONFIG_CAPACITY];
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
        next_config.type = UI_TRACK_TYPE_NONE;
    }

    next_configs[track] = next_config;
    if (track_input_ownership_apply_configs(next_configs) == 0U)
    {
        return false;
    }

    track_state_commit_entry(track,
                             &next_config,
                             track_state_get_midi_channel(track),
                             track_state_get_midi_source(track));
    audio_modulation_projection_publish();
    mod_matrix_publish_control_snapshot_track(track);
    return true;
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

bool track_state_set_track_midi_source(uint8_t track, ui_track_midi_source_t source)
{
    if ((track >= TRACK_CONFIG_CAPACITY) || ((uint8_t)source >= (uint8_t)UI_TRACK_MIDI_SRC_COUNT))
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
    if ((track >= UI_TRACK_COUNT)
            || (entity_topology_is_active(track) == 0U)
            || (track_input_ownership_set_external_input(
                    track, input, g_track_configs) == 0U))
    {
        return false;
    }
    track_state_bump_revision(track);
    return true;
}

bool track_state_apply_bulk(const uint8_t family[UI_TRACK_COUNT],
                            const uint8_t type[UI_TRACK_COUNT],
                            const uint8_t midi_channel[UI_TRACK_COUNT],
                            const uint8_t midi_source[UI_TRACK_COUNT])
{
    uint8_t external_input[UI_TRACK_COUNT];
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        external_input[track] = track_state_get_external_input(track);
    }
    return track_state_apply_bulk_with_inputs(
        family, type, midi_channel, midi_source, external_input);
}

bool track_state_apply_bulk_with_inputs(const uint8_t family[UI_TRACK_COUNT],
                                        const uint8_t type[UI_TRACK_COUNT],
                                        const uint8_t midi_channel[UI_TRACK_COUNT],
                                        const uint8_t midi_source[UI_TRACK_COUNT],
                                        const uint8_t external_input[UI_TRACK_COUNT])
{
    uint8_t entity_family[BRICK_ENTITY_CAPACITY];
    uint8_t entity_type[BRICK_ENTITY_CAPACITY];
    uint8_t entity_channel[BRICK_ENTITY_CAPACITY];
    uint8_t entity_source[BRICK_ENTITY_CAPACITY];
    if ((family == NULL) || (type == NULL) || (midi_channel == NULL) || (midi_source == NULL))
    {
        return false;
    }
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        const ui_track_config_t config = track_state_get_config(track);
        entity_family[track] = (track < UI_TRACK_COUNT) ? family[track] : (uint8_t)config.family;
        entity_type[track] = (track < UI_TRACK_COUNT) ? type[track] : (uint8_t)config.type;
        entity_channel[track] = (track < UI_TRACK_COUNT) ? midi_channel[track] : track_state_get_midi_channel(track);
        entity_source[track] = (track < UI_TRACK_COUNT) ? midi_source[track] : (uint8_t)track_state_get_midi_source(track);
    }
    return track_state_apply_entity_bulk_with_inputs(entity_family, entity_type,
                                                       entity_channel, entity_source,
                                                       external_input);
}

bool track_state_apply_entity_bulk_with_inputs(
    const uint8_t family[BRICK_ENTITY_CAPACITY],
    const uint8_t type[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_channel[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_source[BRICK_ENTITY_CAPACITY],
    const uint8_t external_input[UI_TRACK_COUNT])
{
    if ((family == NULL) || (type == NULL) || (midi_channel == NULL)
            || (midi_source == NULL) || (external_input == NULL))
    {
        return false;
    }

    ui_track_config_t next_configs[TRACK_CONFIG_CAPACITY];
    uint8_t next_channels[TRACK_CONFIG_CAPACITY];
    ui_track_midi_source_t next_sources[TRACK_CONFIG_CAPACITY];

    for (uint8_t track = 0U; track < TRACK_CONFIG_CAPACITY; ++track)
    {
        next_configs[track] = track_state_get_config(track);
        next_channels[track] = track_state_get_midi_channel(track);
        next_sources[track] = track_state_get_midi_source(track);
    }

    for (uint8_t track = 0U; track < TRACK_CONFIG_CAPACITY; ++track)
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
            next_configs[track].family = UI_TRACK_FAMILY_OFF;
            next_configs[track].type = UI_TRACK_TYPE_NONE;
        }
        else
        {
            ui_track_config_t normalized = {
                .family = fam,
                .type = typ
            };
            track_state_normalize_config(&normalized);
            if ((normalized.family != UI_TRACK_FAMILY_OFF)
                    && !ui_track_catalog_type_is_valid_for_family(normalized.family, normalized.type))
            {
                normalized.type = ui_track_catalog_default_type_for_family(normalized.family);
                if (!ui_track_catalog_type_is_valid_for_family(normalized.family, normalized.type))
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
        next_configs[BRICK_ENTITY_GROUP_MASTER_ID].type == UI_TRACK_TYPE_GROUP);
    for (uint8_t track = 0U; track < TRACK_CONFIG_CAPACITY; ++track)
    {
        entity_topology_descriptor_t entity;
        if ((entity_topology_resolve(group_active, track, &entity) == 0U)
                || (entity.active == 0U))
        {
            continue;
        }
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

    if (track_input_ownership_apply_bulk(next_configs, external_input) == 0U)
    {
        return false;
    }

    for (uint8_t track = 0U; track < TRACK_CONFIG_CAPACITY; ++track)
    {
        track_state_commit_entry(track, &next_configs[track], next_channels[track], next_sources[track]);
    }

    audio_modulation_projection_publish();
    mod_matrix_publish_control_snapshot_all();

    return true;
}

uint8_t track_state_count_tracks_with_family(ui_track_family_t family)
{
    if ((uint8_t)family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
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
