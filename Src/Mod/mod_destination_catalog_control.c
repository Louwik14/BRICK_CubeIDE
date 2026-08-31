#include "Mod/mod_destination_control.h"

#include <stddef.h>
#include <stdio.h>

#include "Param/param_registry.h"
#include "Track/entity_topology.h"
#include "Track/track_runtime.h"

/* CONTROL owns presentation and address enumeration.  The list is derived
 * directly from the canonical track descriptor; no AUDIO cache is mirrored. */

static uint8_t mod_destination_control_is_internal_lfo(param_id_t id)
{
    switch (id)
    {
        case PARAM_LFO1_SHAPE: case PARAM_LFO1_TRIG: case PARAM_LFO1_PHASE:
        case PARAM_LFO2_SHAPE: case PARAM_LFO2_TRIG: case PARAM_LFO2_PHASE:
        case PARAM_LFO3_SHAPE: case PARAM_LFO3_TRIG: case PARAM_LFO3_PHASE:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_control_is_structural_sampler(param_id_t id)
{
    switch (id)
    {
        case PARAM_SAMPLER_MODE:
        case PARAM_SAMPLER_SLICE_COUNT:
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
        case PARAM_SAMPLER_CLIP_PITCH:
        case PARAM_SAMPLER_CLIP_PLAY_MODE:
        case PARAM_SAMPLER_CLIP_LOOP:
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
        case PARAM_SAMPLER_CLIP_GRAIN:
        case PARAM_SAMPLER_CLIP_HOP:
        case PARAM_SAMPLER_CLIP_SEARCH:
        case PARAM_SAMPLER_MULTI_LOOP:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_control_supported(uint8_t track, param_id_t id)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (id >= PARAM_COUNT)
            || (mod_destination_control_is_internal_lfo(id) != 0U)
            || (mod_destination_control_is_structural_sampler(id) != 0U))
        return 0U;
    if ((id == PARAM_LFO1_RATE) || (id == PARAM_LFO2_RATE)
            || (id == PARAM_LFO3_RATE))
        return 1U;

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_ENV)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX))
        return 0U;
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
    {
        track_runtime_descriptor_t descriptor;
        uint8_t slot = 0U;
        if ((track_runtime_get_descriptor(track, &descriptor) == 0U)
                || (track_runtime_tone_param_to_slot(
                    descriptor.type, id, &slot) == 0U))
            return 0U;
    }
    const track_runtime_param_status_t status =
        track_runtime_get_effective_param_status(track, id);
    return ((status == TRACK_RUNTIME_PARAM_ALLOWED)
            || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

static uint16_t mod_destination_control_count_local(uint8_t track)
{
    uint16_t count = 1U;
    for (uint16_t raw = 0U; raw < (uint16_t)PARAM_COUNT; ++raw)
        if (mod_destination_control_supported(track, (param_id_t)raw) != 0U)
            ++count;
    return count;
}

static param_id_t mod_destination_control_param_local(uint8_t track,
                                                       uint16_t index)
{
    if (index == 0U) return MOD_DESTINATION_NONE;
    uint16_t cursor = 1U;
    for (uint16_t raw = 0U; raw < (uint16_t)PARAM_COUNT; ++raw)
    {
        if (mod_destination_control_supported(track, (param_id_t)raw) == 0U)
            continue;
        if (cursor++ == index) return (param_id_t)raw;
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
    *out_name = param_registry[param].name;
    return (*out_name != NULL) ? 1U : 0U;
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
