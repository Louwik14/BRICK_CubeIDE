#include "Core/entity_topology.h"

#include <stddef.h>

#include "Core/track_state.h"

uint8_t entity_topology_resolve(uint8_t group_active,
                                brick_entity_id_t entity_id,
                                entity_topology_descriptor_t *out_descriptor)
{
    if ((out_descriptor == NULL) || (entity_id >= BRICK_ENTITY_CAPACITY))
    {
        return 0U;
    }

    out_descriptor->entity_id = entity_id;
    out_descriptor->parent_entity_id = BRICK_ENTITY_INVALID_ID;
    out_descriptor->member_index = BRICK_ENTITY_INVALID_ID;
    out_descriptor->role = ENTITY_ROLE_MAIN;
    out_descriptor->active = 0U;

    if (entity_id < BRICK_ENTITY_TOP_LEVEL_COUNT)
    {
        out_descriptor->active = 1U;
        if ((group_active != 0U) && (entity_id == BRICK_ENTITY_GROUP_MASTER_ID))
        {
            out_descriptor->role = ENTITY_ROLE_GROUP_MASTER;
        }
        return 1U;
    }

    if (group_active != 0U)
    {
        out_descriptor->parent_entity_id = BRICK_ENTITY_GROUP_MASTER_ID;
        out_descriptor->member_index =
            (uint8_t)(entity_id - BRICK_ENTITY_FIRST_GROUP_CHILD_ID);
        out_descriptor->role = ENTITY_ROLE_GROUP_CHILD;
        out_descriptor->active = 1U;
    }

    return 1U;
}

uint8_t entity_topology_group_is_active(void)
{
    return (track_state_get_type(BRICK_ENTITY_GROUP_MASTER_ID)
            == UI_TRACK_TYPE_GROUP) ? 1U : 0U;
}

uint8_t entity_topology_get(brick_entity_id_t entity_id,
                            entity_topology_descriptor_t *out_descriptor)
{
    return entity_topology_resolve(entity_topology_group_is_active(),
                                   entity_id,
                                   out_descriptor);
}

uint8_t entity_topology_is_active(brick_entity_id_t entity_id)
{
    entity_topology_descriptor_t descriptor;
    return (uint8_t)((entity_topology_get(entity_id, &descriptor) != 0U)
            && (descriptor.active != 0U));
}

uint8_t entity_topology_can_sequence(const entity_topology_descriptor_t *descriptor)
{
    return (uint8_t)((descriptor != NULL) && (descriptor->active != 0U));
}

uint8_t entity_topology_can_emit_notes(const entity_topology_descriptor_t *descriptor)
{
    return (uint8_t)((descriptor != NULL)
            && (descriptor->active != 0U)
            && (descriptor->role != ENTITY_ROLE_GROUP_MASTER));
}

uint8_t entity_topology_has_audio_source(const entity_topology_descriptor_t *descriptor)
{
    return entity_topology_can_emit_notes(descriptor);
}

uint16_t entity_topology_get_capabilities(const entity_topology_descriptor_t *descriptor)
{
    if ((descriptor == NULL) || (descriptor->active == 0U))
    {
        return 0U;
    }

    uint16_t capabilities = (uint16_t)(TRACK_CAPABILITY_MIDI
            | TRACK_CAPABILITY_KEYBOARD
            | TRACK_CAPABILITY_MIDI_FX
            | TRACK_CAPABILITY_MUTE);

    if (entity_topology_can_emit_notes(descriptor) != 0U)
    {
        capabilities |= TRACK_CAPABILITY_NOTES;
    }
    if (entity_topology_has_audio_source(descriptor) != 0U)
    {
        capabilities |= TRACK_CAPABILITY_AUDIO;
    }
    if (descriptor->role != ENTITY_ROLE_GROUP_CHILD)
    {
        capabilities |= TRACK_CAPABILITY_INPUT_RESERVATION;
    }

    return capabilities;
}

uint8_t entity_topology_has_capability(brick_entity_id_t entity_id,
                                       track_capability_t capability)
{
    entity_topology_descriptor_t descriptor;
    return (uint8_t)((entity_topology_get(entity_id, &descriptor) != 0U)
            && ((entity_topology_get_capabilities(&descriptor)
                & (uint16_t)capability) != 0U));
}

uint8_t entity_topology_get_top_level_count(void)
{
    return BRICK_ENTITY_TOP_LEVEL_COUNT;
}

uint8_t entity_topology_get_physical_input_count(void)
{
    return ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT;
}
