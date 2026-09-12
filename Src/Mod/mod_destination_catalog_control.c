#include "Mod/mod_destination_control.h"

#include <stddef.h>
#include <stdio.h>

#include "Param/param_registry.h"
#include "Track/entity_topology.h"
#include "Track/track_runtime.h"

/* CONTROL owns presentation and address enumeration.  The list is derived
 * directly from the canonical track descriptor; no AUDIO cache is mirrored. */

static uint8_t mod_destination_control_supported(uint8_t track, param_id_t id)
{
    param_registry_resolved_track_param_t resolved;
    return (uint8_t)((param_registry_resolve_track_param(
            track, id, &resolved) != 0U)
        && (resolved.applicable != 0U) && (resolved.plockable != 0U));
}

uint8_t mod_destination_catalog_address_is_supported_projected(
    uint8_t owner, mod_destination_address_t address,
    const track_config_t configs[BRICK_ENTITY_CAPACITY])
{
    if ((owner >= BRICK_ENTITY_CAPACITY) || (configs == NULL)) return 0U;
    if (address == MOD_DESTINATION_NONE) return 1U;
    uint8_t target = BRICK_ENTITY_INVALID_ID;
    param_id_t id = PARAM_COUNT;
    if (mod_destination_address_resolve(address, &target, &id) == 0U) return 0U;
    const uint8_t group_active = (uint8_t)(
        configs[BRICK_ENTITY_GROUP_MASTER_ID].type == TRACK_TYPE_GROUP);
    entity_topology_descriptor_t owner_topology, target_topology;
    if ((entity_topology_resolve(group_active, owner, &owner_topology) == 0U)
            || (entity_topology_resolve(group_active, target, &target_topology) == 0U)
            || (owner_topology.active == 0U) || (target_topology.active == 0U))
        return 0U;
    if ((owner_topology.role == ENTITY_ROLE_GROUP_MASTER)
            ? !((target == owner)
                || (target_topology.parent_entity_id == owner))
            : (target != owner))
        return 0U;
    const track_config_t config = configs[target];
    return param_registry_projected_track_param_is_applicable(id,
        config.family, track_runtime_type_from_ui(config.type),
        (uint8_t)(target_topology.role == ENTITY_ROLE_GROUP_MASTER),
        (uint8_t)(target_topology.role == ENTITY_ROLE_GROUP_CHILD));
}

static uint16_t mod_destination_control_candidate_count(void)
{
    return (uint16_t)PARAM_COUNT;
}

static param_id_t mod_destination_control_candidate_at(uint16_t index)
{
    return (index < (uint16_t)PARAM_COUNT) ? (param_id_t)index : PARAM_COUNT;
}

static uint16_t mod_destination_control_count_local(uint8_t track)
{
    uint16_t count = 1U;
    const uint16_t candidate_count = mod_destination_control_candidate_count();
    for (uint16_t i = 0U; i < candidate_count; ++i)
        if (mod_destination_control_supported(
                track, mod_destination_control_candidate_at(i)) != 0U)
            ++count;
    return count;
}

static param_id_t mod_destination_control_param_local(uint8_t track,
                                                       uint16_t index)
{
    if (index == 0U) return MOD_DESTINATION_NONE;
    uint16_t cursor = 1U;
    const uint16_t candidate_count = mod_destination_control_candidate_count();
    for (uint16_t i = 0U; i < candidate_count; ++i)
    {
        const param_id_t id = mod_destination_control_candidate_at(i);
        if (mod_destination_control_supported(track, id) == 0U)
            continue;
        if (cursor++ == index) return id;
    }
    return MOD_DESTINATION_NONE;
}

uint16_t mod_destination_catalog_count(uint8_t track)
{
    entity_topology_descriptor_t topology;
    if ((entity_topology_get(track, &topology) != 0U)
            && (topology.role == ENTITY_ROLE_GROUP_MASTER))
    {
        uint16_t count = 1U;
        for (uint8_t target = BRICK_ENTITY_GROUP_MASTER_ID;
             target < BRICK_ENTITY_CAPACITY; ++target)
            count = (uint16_t)(count
                + mod_destination_control_count_local(target) - 1U);
        return count;
    }
    return mod_destination_control_count_local(track);
}

mod_destination_address_t mod_destination_catalog_address_from_index(
    uint8_t owner, uint16_t index)
{
    if ((owner >= BRICK_ENTITY_CAPACITY) || (index == 0U))
        return MOD_DESTINATION_NONE;
    entity_topology_descriptor_t topology;
    if ((entity_topology_get(owner, &topology) != 0U)
            && (topology.role == ENTITY_ROLE_GROUP_MASTER))
    {
        uint16_t cursor = 1U;
        for (uint8_t target = BRICK_ENTITY_GROUP_MASTER_ID;
             target < BRICK_ENTITY_CAPACITY; ++target)
        {
            const uint16_t local_count =
                (uint16_t)(mod_destination_control_count_local(target) - 1U);
            if (index < (uint16_t)(cursor + local_count))
                return mod_destination_address_make(target,
                    mod_destination_control_param_local(
                        target, (uint16_t)(index - cursor + 1U)));
            cursor = (uint16_t)(cursor + local_count);
        }
        return MOD_DESTINATION_NONE;
    }
    return mod_destination_address_make(
        owner, mod_destination_control_param_local(owner, index));
}

param_id_t mod_destination_catalog_param_from_index(uint8_t track,
                                                     uint16_t index)
{
    uint8_t target = 0U;
    param_id_t param = PARAM_COUNT;
    return (mod_destination_address_resolve(
        mod_destination_catalog_address_from_index(track, index),
        &target, &param) != 0U) ? param : MOD_DESTINATION_NONE;
}

uint16_t mod_destination_catalog_index_from_address(
    uint8_t owner, mod_destination_address_t address)
{
    if ((owner >= BRICK_ENTITY_CAPACITY) || (address == MOD_DESTINATION_NONE))
        return 0U;
    const uint16_t count = mod_destination_catalog_count(owner);
    for (uint16_t index = 1U; index < count; ++index)
        if (mod_destination_catalog_address_from_index(owner, index) == address)
            return index;
    return 0U;
}

uint16_t mod_destination_catalog_index_from_param(uint8_t track,
                                                   param_id_t param)
{
    return mod_destination_catalog_index_from_address(
        track, mod_destination_address_make(track, param));
}

static uint8_t mod_destination_control_resolve_label(
    uint8_t owner, uint16_t index, uint8_t *out_target, const char **out_name)
{
    param_id_t param = PARAM_COUNT;
    const mod_destination_address_t address =
        mod_destination_catalog_address_from_index(owner, index);
    if (address == MOD_DESTINATION_NONE)
    {
        *out_target = owner;
        *out_name = "Off";
        return 1U;
    }
    if (mod_destination_address_resolve(address, out_target, &param) == 0U)
        return 0U;
    param_registry_resolved_track_param_t resolved;
    if ((param_registry_resolve_track_param(
            *out_target, param, &resolved) == 0U)
            || (resolved.applicable == 0U) || (resolved.label == NULL))
        return 0U;
    *out_name = resolved.label;
    return 1U;
}

uint8_t mod_destination_catalog_label(uint8_t track, uint16_t index,
                                      char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U)) return 0U;
    uint8_t target = track;
    const char *name = NULL;
    if (mod_destination_control_resolve_label(
            track, index, &target, &name) == 0U) return 0U;
    entity_topology_descriptor_t owner;
    if ((entity_topology_get(track, &owner) != 0U)
            && (owner.role == ENTITY_ROLE_GROUP_MASTER)
            && (index != 0U))
    {
        entity_topology_descriptor_t target_topology;
        if (entity_topology_get(target, &target_topology) == 0U) return 0U;
        if (target_topology.role == ENTITY_ROLE_GROUP_MASTER)
            (void)snprintf(out, out_len, "MASTER %s", name);
        else
            (void)snprintf(out, out_len, "SUB%u %s",
                (unsigned int)target_topology.member_index + 1U, name);
    }
    else
        (void)snprintf(out, out_len, "%s", name);
    return 1U;
}

uint8_t mod_destination_catalog_short_label(uint8_t track, uint16_t index,
                                            char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U)) return 0U;
    uint8_t target = track;
    const char *name = NULL;
    if (mod_destination_control_resolve_label(
            track, index, &target, &name) == 0U) return 0U;
    uint32_t i = 0U;
    for (; (i < 4U) && ((i + 1U) < out_len) && (name[i] != '\0'); ++i)
        out[i] = name[i];
    out[i] = '\0';
    return 1U;
}

void mod_destination_catalog_init(void) {}
void mod_destination_catalog_invalidate_track(uint8_t track) { (void)track; }
void mod_destination_catalog_invalidate_all(void) {}
