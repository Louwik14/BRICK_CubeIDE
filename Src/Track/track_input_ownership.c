#include "Track/track_input_ownership.h"

#include <string.h>
#include <assert.h>

#include "Track/entity_topology.h"
#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "IPC/live_clock_control.h"

static uint8_t g_external_input[UI_TRACK_COUNT];
static uint8_t g_external_owner[ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT];

static uint8_t track_input_ownership_is_external(const ui_track_config_t *config)
{
    return (uint8_t)((config != NULL)
            && (config->family == UI_TRACK_FAMILY_EXTERNAL)
            && (config->type == UI_TRACK_TYPE_EXTERNAL));
}

static uint8_t track_input_ownership_build(
    const ui_track_config_t configs[UI_TRACK_COUNT],
    const uint8_t selected[UI_TRACK_COUNT],
    uint8_t owners[ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT])
{
    if ((configs == NULL) || (selected == NULL) || (owners == NULL))
    {
        return 0U;
    }

    memset(owners, TRACK_INPUT_OWNER_NONE, ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT);
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if (track_input_ownership_is_external(&configs[track]) == 0U)
        {
            continue;
        }

        const uint8_t input = selected[track];
        if ((input >= ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT)
                || (owners[input] != TRACK_INPUT_OWNER_NONE))
        {
            return 0U;
        }
        owners[input] = track;
    }
    return 1U;
}

void track_input_ownership_init(const ui_track_config_t configs[UI_TRACK_COUNT])
{
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        g_external_input[track] = (uint8_t)(track % ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT);
    }
    memset(g_external_owner, TRACK_INPUT_OWNER_NONE, sizeof(g_external_owner));
    (void)track_input_ownership_apply_configs(configs);
}

uint8_t track_input_ownership_apply_configs(
    const ui_track_config_t configs[UI_TRACK_COUNT])
{
    return track_input_ownership_apply_bulk(configs, g_external_input);
}

uint8_t track_input_ownership_apply_bulk(
    const ui_track_config_t configs[UI_TRACK_COUNT],
    const uint8_t external_input[UI_TRACK_COUNT])
{
    uint8_t next_selected[UI_TRACK_COUNT];
    uint8_t next_owners[ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT];
    if ((configs == NULL) || (external_input == NULL))
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const uint8_t input = external_input[track];
        if (input >= ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT)
        {
            return 0U;
        }
        next_selected[track] = input;
    }
    if (track_input_ownership_build(configs, next_selected, next_owners) == 0U)
    {
        return 0U;
    }

    uint64_t sample_time = 0U;
    if (!live_clock_read_audio_sample(&sample_time)) return 0U;
    control_audio_command_t commands[ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT];
    uint8_t command_count = 0U;
    for (uint8_t input = 0U;
         input < ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT; ++input)
    {
        if (next_owners[input] == g_external_owner[input]) continue;
        commands[command_count++] = (control_audio_command_t){
            .effective_sample_time = sample_time,
            .value = next_owners[input],
            .id = CONTROL_AUDIO_PARAM_INPUT_OWNER,
            .entity = input,
            .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
                CONTROL_AUDIO_COMMAND_PARAM, 0U)
        };
    }
    if ((command_count != 0U)
            && (control_audio_publish_batch(commands, command_count) == 0U))
    {
        assert(0 && "input ownership publication capacity invariant");
        return 0U;
    }
    memcpy(g_external_input, next_selected, sizeof(g_external_input));
    memcpy(g_external_owner, next_owners, sizeof(g_external_owner));
    return 1U;
}

uint8_t track_input_ownership_validate_bulk(
    const ui_track_config_t configs[UI_TRACK_COUNT],
    const uint8_t external_input[UI_TRACK_COUNT])
{
    uint8_t owners[ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT];
    if ((configs == NULL) || (external_input == NULL))
    {
        return 0U;
    }
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        if (external_input[track] >= ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT)
        {
            return 0U;
        }
    }
    return track_input_ownership_build(configs, external_input, owners);
}

uint8_t track_input_ownership_can_claim(uint8_t track, uint8_t input)
{
    if ((track >= UI_TRACK_COUNT)
            || (input >= ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT))
    {
        return 0U;
    }
    return (uint8_t)((g_external_owner[input] == TRACK_INPUT_OWNER_NONE)
            || (g_external_owner[input] == track));
}

uint8_t track_input_ownership_set_external_input(
    uint8_t track,
    uint8_t input,
    const ui_track_config_t configs[UI_TRACK_COUNT])
{
    uint8_t selected[UI_TRACK_COUNT];
    if ((track >= UI_TRACK_COUNT)
            || (input >= ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT)
            || (configs == NULL))
    {
        return 0U;
    }

    memcpy(selected, g_external_input, sizeof(selected));
    selected[track] = input;
    return track_input_ownership_apply_bulk(configs, selected);
}

uint8_t track_input_ownership_get_external_input(uint8_t track)
{
    return (track < UI_TRACK_COUNT) ? g_external_input[track] : 0U;
}

uint8_t track_input_ownership_get_external_owner(uint8_t input, uint8_t *out_track)
{
    if ((input >= ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT)
            || (g_external_owner[input] == TRACK_INPUT_OWNER_NONE))
    {
        return 0U;
    }
    if (out_track != NULL)
    {
        *out_track = g_external_owner[input];
    }
    return 1U;
}

uint8_t track_input_ownership_track_owns_input(uint8_t track, uint8_t input)
{
    uint8_t owner = TRACK_INPUT_OWNER_NONE;
    return (uint8_t)((track_input_ownership_get_external_owner(input, &owner) != 0U)
            && (owner == track));
}
