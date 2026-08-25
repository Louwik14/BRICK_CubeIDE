#include "Storage/persistent_entity_topology.h"

#include <stddef.h>

_Static_assert(PERSIST_CONTROL_ENTITY_COUNT == BRICK_ENTITY_CAPACITY,
               "persistence and product entity capacities diverged");
_Static_assert(PERSIST_CONTROL_TOP_LEVEL_COUNT == BRICK_ENTITY_TOP_LEVEL_COUNT,
               "persistence and product top-level counts diverged");
_Static_assert(PERSIST_CONTROL_GROUP_MASTER_ID == BRICK_ENTITY_GROUP_MASTER_ID,
               "persistence and product GROUP master identities diverged");
_Static_assert(PERSIST_CONTROL_FIRST_GROUP_CHILD_ID == BRICK_ENTITY_FIRST_GROUP_CHILD_ID,
               "persistence and product GROUP child identities diverged");

uint8_t persist_entity_caps_resolve(uint8_t group_active,
                                    persist_control_entity_id_t entity_id,
                                    persist_entity_caps_t *out_caps)
{
    entity_topology_descriptor_t topology;
    if ((out_caps == NULL)
            || (entity_topology_resolve(group_active, entity_id, &topology) == 0U))
    {
        return 0U;
    }

    persist_control_entity_role_t role = PERSIST_ENTITY_ROLE_MAIN;
    if (topology.role == ENTITY_ROLE_GROUP_MASTER)
        role = PERSIST_ENTITY_ROLE_GROUP_MASTER;
    else if (topology.role == ENTITY_ROLE_GROUP_CHILD)
        role = PERSIST_ENTITY_ROLE_GROUP_CHILD;

    const uint8_t top_level = (uint8_t)(
        entity_id < PERSIST_CONTROL_FIRST_GROUP_CHILD_ID);
    *out_caps = (persist_entity_caps_t){
        .entity_id = entity_id,
        .parent_entity_id = topology.parent_entity_id,
        .role = role,
        .exists = 1U,
        .active = topology.active,
        .persistable = 1U,
        .sequence_owner = topology.active,
        .modulation_owner = (uint8_t)(topology.active && top_level),
        .audio_fx_owner = (uint8_t)(topology.active && top_level),
        .note_fx_owner = (uint8_t)(topology.active
            && (topology.role != ENTITY_ROLE_GROUP_MASTER)),
        .input_owner = (uint8_t)(topology.active
            && (topology.role != ENTITY_ROLE_GROUP_CHILD)),
        .play_limit = (topology.active == 0U) ? 0U
            : ((topology.role == ENTITY_ROLE_GROUP_CHILD)
                ? PERSIST_CONTROL_CHILD_PLAY_ITEM_COUNT
                : PERSIST_CONTROL_PLAY_ITEM_COUNT)
    };
    return 1U;
}

uint8_t persist_entity_mod_destination_allowed(
    uint8_t group_active,
    persist_control_entity_id_t owner_id,
    persist_control_entity_id_t destination_id)
{
    persist_entity_caps_t owner;
    persist_entity_caps_t destination;
    if ((persist_entity_caps_resolve(group_active, owner_id, &owner) == 0U)
            || (persist_entity_caps_resolve(
                group_active, destination_id, &destination) == 0U)
            || (owner.modulation_owner == 0U)
            || (destination.active == 0U))
    {
        return 0U;
    }

    if (owner.role != PERSIST_ENTITY_ROLE_GROUP_MASTER)
        return (destination_id == owner_id) ? 1U : 0U;

    return (uint8_t)((destination_id == owner_id)
        || ((destination.role == PERSIST_ENTITY_ROLE_GROUP_CHILD)
            && (destination.parent_entity_id == owner_id)));
}
