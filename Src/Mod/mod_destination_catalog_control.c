#include "Mod/mod_destination_control.h"

#include <stddef.h>
#include <stdio.h>

#include "Param/param_registry.h"
#include "Seq/seq_param_iface.h"
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

#define MOD_DESTINATION_LOCAL_CAPACITY \
    (SEQ_PARAM_ENV_SLOT_COUNT + SEQ_PARAM_TONE_SLOT_COUNT \
     + SEQ_PARAM_MOD_SLOT_COUNT + SEQ_PARAM_MIX_SLOT_COUNT \
     + SEQ_PARAM_FM_OPERATOR_SLOT_COUNT + SEQ_PARAM_AUDIO_FX_SLOT_COUNT)

typedef struct
{
    uint8_t valid;
    uint16_t count;
    param_id_t param[MOD_DESTINATION_LOCAL_CAPACITY];
} mod_destination_local_catalog_t;

static mod_destination_local_catalog_t
    g_mod_destination_local_catalog[BRICK_ENTITY_CAPACITY];

static uint8_t mod_destination_control_set_capacity(uint8_t set_id)
{
    switch ((seq_plock_set_id_t)set_id)
    {
        case SEQ_PLOCK_SET_MIX: return SEQ_PARAM_MIX_SLOT_COUNT;
        case SEQ_PLOCK_SET_ENV: return SEQ_PARAM_ENV_SLOT_COUNT;
        case SEQ_PLOCK_SET_TONE: return SEQ_PARAM_TONE_SLOT_COUNT;
        case SEQ_PLOCK_SET_MOD: return SEQ_PARAM_MOD_SLOT_COUNT;
        case SEQ_PLOCK_SET_FM_OPERATOR: return SEQ_PARAM_FM_OPERATOR_SLOT_COUNT;
        case SEQ_PLOCK_SET_AUDIO_FX: return SEQ_PARAM_AUDIO_FX_SLOT_COUNT;
        default: return 0U;
    }
}

static uint8_t mod_destination_control_catalog_contains(
    const mod_destination_local_catalog_t *catalog, param_id_t id)
{
    for (uint16_t i = 0U; i < catalog->count; ++i)
        if (catalog->param[i] == id) return 1U;
    return 0U;
}

static const mod_destination_local_catalog_t *
mod_destination_control_local_catalog(uint8_t track)
{
    if (track >= BRICK_ENTITY_CAPACITY) return NULL;
    mod_destination_local_catalog_t *const catalog =
        &g_mod_destination_local_catalog[track];
    if (catalog->valid != 0U) return catalog;

    static const uint8_t set_order[] = {
        (uint8_t)SEQ_PLOCK_SET_MIX,
        (uint8_t)SEQ_PLOCK_SET_ENV,
        (uint8_t)SEQ_PLOCK_SET_TONE,
        (uint8_t)SEQ_PLOCK_SET_MOD,
        (uint8_t)SEQ_PLOCK_SET_FM_OPERATOR,
        (uint8_t)SEQ_PLOCK_SET_AUDIO_FX
    };
    catalog->count = 0U;
    for (uint8_t set = 0U;
         set < (uint8_t)(sizeof(set_order) / sizeof(set_order[0])); ++set)
    {
        const uint8_t set_id = set_order[set];
        const uint8_t capacity = mod_destination_control_set_capacity(set_id);
        for (uint8_t slot = 0U; slot < capacity; ++slot)
        {
            param_id_t id = PARAM_COUNT;
            if ((seq_param_iface_slot_to_param(track, set_id, slot, &id) == 0U)
                    || (mod_destination_control_supported(track, id) == 0U)
                    || (mod_destination_control_catalog_contains(catalog, id) != 0U))
                continue;
            if (catalog->count >= MOD_DESTINATION_LOCAL_CAPACITY) return NULL;
            catalog->param[catalog->count++] = id;
        }
    }
    catalog->valid = 1U;
    return catalog;
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

static uint16_t mod_destination_control_count_local(uint8_t track)
{
    const mod_destination_local_catalog_t *const catalog =
        mod_destination_control_local_catalog(track);
    return (catalog != NULL) ? (uint16_t)(catalog->count + 1U) : 1U;
}

static param_id_t mod_destination_control_param_local(uint8_t track,
                                                       uint16_t index)
{
    const mod_destination_local_catalog_t *const catalog =
        mod_destination_control_local_catalog(track);
    return ((catalog != NULL) && (index > 0U) && (index <= catalog->count))
        ? catalog->param[index - 1U] : MOD_DESTINATION_NONE;
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
    uint8_t target = BRICK_ENTITY_INVALID_ID;
    param_id_t param = PARAM_COUNT;
    if (mod_destination_address_resolve(address, &target, &param) == 0U)
        return 0U;
    entity_topology_descriptor_t topology;
    uint16_t offset = 1U;
    if ((entity_topology_get(owner, &topology) != 0U)
            && (topology.role == ENTITY_ROLE_GROUP_MASTER))
    {
        if (target < BRICK_ENTITY_GROUP_MASTER_ID) return 0U;
        for (uint8_t candidate = BRICK_ENTITY_GROUP_MASTER_ID;
             candidate < target; ++candidate)
            offset = (uint16_t)(offset
                + mod_destination_control_count_local(candidate) - 1U);
    }
    else if (target != owner)
        return 0U;
    const mod_destination_local_catalog_t *const catalog =
        mod_destination_control_local_catalog(target);
    if (catalog == NULL) return 0U;
    for (uint16_t index = 0U; index < catalog->count; ++index)
        if (catalog->param[index] == param)
            return (uint16_t)(offset + index);
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

void mod_destination_catalog_init(void)
{
    mod_destination_catalog_invalidate_all();
}

void mod_destination_catalog_invalidate_track(uint8_t track)
{
    if (track < BRICK_ENTITY_CAPACITY)
        g_mod_destination_local_catalog[track].valid = 0U;
}

void mod_destination_catalog_invalidate_all(void)
{
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
        g_mod_destination_local_catalog[track].valid = 0U;
}
