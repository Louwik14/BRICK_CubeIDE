#include "Mod/mod_destination_control.h"

#include <stddef.h>
#include <stdio.h>

#include "Param/param_registry.h"
#include "Track/entity_topology.h"
#include "Track/track_runtime.h"
#include "Track/tone_param_codec.h"

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
        case PARAM_SAMPLER_MULTI_LOOP:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_destination_control_supported(uint8_t track, param_id_t id)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (id >= PARAM_COUNT)
            || (id == PARAM_AUDIO_FX_MODEL)
            || (id == PARAM_AUDIO_FX_B_MODEL)
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
                || (tone_param_codec_param_to_slot(
                    descriptor.type, id, &slot) == 0U))
            return 0U;
    }
    const track_runtime_param_status_t status =
        track_runtime_get_effective_param_status(track, id);
    return ((status == TRACK_RUNTIME_PARAM_ALLOWED)
            || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
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
    if ((id == PARAM_AUDIO_FX_MODEL) || (id == PARAM_AUDIO_FX_B_MODEL)
            || (mod_destination_control_is_internal_lfo(id) != 0U)
            || (mod_destination_control_is_structural_sampler(id) != 0U))
        return 0U;
    if ((id == PARAM_LFO1_RATE) || (id == PARAM_LFO2_RATE)
            || (id == PARAM_LFO3_RATE)) return 1U;
    const track_config_t config = configs[target];
    if (config.family == TRACK_FAMILY_OFF) return 0U;
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_ENV)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX))
        return 0U;
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
    {
        uint8_t slot = 0U;
        return tone_param_codec_param_to_slot(
            track_runtime_type_from_ui(config.type), id, &slot);
    }
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX)
    {
        const uint8_t group_level = (uint8_t)((id == PARAM_GROUP_FX_A_LEVEL)
            || (id == PARAM_GROUP_FX_B_LEVEL));
        return (target_topology.role == ENTITY_ROLE_GROUP_CHILD)
            ? group_level : (uint8_t)(group_level == 0U);
    }
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
        return (uint8_t)(config.family != TRACK_FAMILY_MIDI);
    if (target_topology.role == ENTITY_ROLE_GROUP_MASTER)
        return (uint8_t)((id == PARAM_FILTER_MORPH)
            || (id == PARAM_FILTER_CUTOFF) || (id == PARAM_FILTER_RESONANCE)
            || (id == PARAM_FILTER_KEYTRK) || (id == PARAM_ENV3_ATTACK)
            || (id == PARAM_ENV3_DECAY) || (id == PARAM_ENV3_SUSTAIN)
            || (id == PARAM_ENV3_RELEASE) || (id == PARAM_ENV_RETRIG_MOD));
    if (rule.resource == TRACK_RUNTIME_RESOURCE_FILTER)
        return (uint8_t)((config.family == TRACK_FAMILY_SYNTH)
            || (config.family == TRACK_FAMILY_SAMPLER)
            || (config.family == TRACK_FAMILY_DRUM)
            || (config.family == TRACK_FAMILY_EXTERNAL));
    return 1U;
}

typedef struct
{
    param_id_t first;
    param_id_t last;
} mod_destination_param_range_t;

/* Candidate ranges are the owner domains, kept in stable parameter order.
 * Applicability remains owned by mod_destination_control_supported(). */
static const mod_destination_param_range_t g_mod_destination_param_ranges[] = {
    { PARAM_MIX_MUTE, PARAM_MIX_MUTE },
    { PARAM_DRUM_MD_MODEL, PARAM_DRUM_MD_P8 },
    { PARAM_MIX_LEVEL, PARAM_MIX_SEND2 },
    { PARAM_FILTER_MORPH, PARAM_ENV_RETRIG_MOD },
    { PARAM_SAMPLER_GAIN, PARAM_SAMPLER_CLIP_GRAIN },
    { PARAM_FILTER_MODE, PARAM_FM_ENV_RELEASE },
    { PARAM_LOOPER_XFADE, PARAM_WAVE_DETUNE },
    { PARAM_FM_PLAY_VEL, PARAM_FM_OPERATOR_LAST },
    { PARAM_AUDIO_FX_P1, PARAM_GROUP_FX_B_LEVEL },
};

static uint16_t mod_destination_control_candidate_count(void)
{
    uint16_t count = 0U;
    for (uint8_t i = 0U;
         i < (uint8_t)(sizeof(g_mod_destination_param_ranges)
                       / sizeof(g_mod_destination_param_ranges[0])); ++i)
    {
        count = (uint16_t)(count
            + (g_mod_destination_param_ranges[i].last
               - g_mod_destination_param_ranges[i].first + 1U));
    }
    return count;
}

static param_id_t mod_destination_control_candidate_at(uint16_t index)
{
    for (uint8_t i = 0U;
         i < (uint8_t)(sizeof(g_mod_destination_param_ranges)
                       / sizeof(g_mod_destination_param_ranges[0])); ++i)
    {
        const mod_destination_param_range_t range =
            g_mod_destination_param_ranges[i];
        const uint16_t count = (uint16_t)(range.last - range.first + 1U);
        if (index < count) return (param_id_t)(range.first + index);
        index = (uint16_t)(index - count);
    }
    return PARAM_COUNT;
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
