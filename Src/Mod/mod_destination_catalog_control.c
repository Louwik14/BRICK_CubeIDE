#include "Mod/mod_destination_control.h"

#include <stddef.h>
#include <stdio.h>

#include "Param/param_registry.h"
#include "Track/entity_topology.h"
#include "Track/track_runtime.h"
#include "Track/audio_fx_control_state.h"
#include "Param/audio_fx_param_catalog.h"
#include "Param/md_model_catalog.h"
#include "Param/param_prism_labels.h"
#include "Param/param_stack_labels.h"
#include "Param/tone_param_catalog.h"
#include "Seq/seq_param_iface.h"

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

static uint8_t mod_destination_control_supported(uint8_t track, param_id_t id)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (id >= PARAM_COUNT)
            || (id == PARAM_MIDI_PROGRAM)
            || (mod_destination_control_is_internal_lfo(id) != 0U))
        return 0U;
    if ((id == PARAM_LFO1_RATE) || (id == PARAM_LFO2_RATE)
            || (id == PARAM_LFO3_RATE))
        return 1U;
    uint8_t fx_slot = 0U;
    uint8_t fx_param = 0U;
    if (audio_fx_param_catalog_param_info(id, &fx_slot, &fx_param) != 0U)
    {
        float model = 0.0f;
        if ((audio_fx_control_state_get_param(track,
                (fx_slot != 0U) ? PARAM_AUDIO_FX_B_MODEL
                                : PARAM_AUDIO_FX_MODEL, &model) == 0U)
                || (fx_param >= audio_fx_param_catalog_count(
                    (uint8_t)(model + 0.5f))))
            return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_ENV)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX))
        return 0U;
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
    {
        track_runtime_descriptor_t descriptor;
        if ((track_runtime_get_descriptor(track, &descriptor) == 0U)
                || (tone_param_catalog_is_user_visible(
                    descriptor.type, id) == 0U))
            return 0U;
        if ((descriptor.type == TRACK_RUNTIME_TYPE_DRUM_MD)
                && (id >= PARAM_DRUM_MD_P1) && (id <= PARAM_DRUM_MD_P8))
        {
            float model = 0.0f;
            if ((param_registry_get_track_value(
                    PARAM_DRUM_MD_MODEL, track, &model) == 0U)
                    || ((uint8_t)(id - PARAM_DRUM_MD_P1)
                        >= md_model_profile_get(md_model_validate(model))->slot_count))
                return 0U;
        }
    }
    uint8_t set_id = (uint8_t)SEQ_PLOCK_SET_COUNT;
    if ((id >= PARAM_FM_OPERATOR_FIRST) && (id <= PARAM_FM_OPERATOR_LAST))
        set_id = (uint8_t)SEQ_PLOCK_SET_FM_OPERATOR;
    else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
        set_id = (uint8_t)SEQ_PLOCK_SET_ENV;
    else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        set_id = (uint8_t)SEQ_PLOCK_SET_TONE;
    else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
        set_id = (uint8_t)SEQ_PLOCK_SET_MIX;
    else if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX)
        set_id = (uint8_t)SEQ_PLOCK_SET_AUDIO_FX;
    if ((set_id >= (uint8_t)SEQ_PLOCK_SET_COUNT)
            || (seq_param_iface_param_is_supported(track, set_id, id) == 0U))
        return 0U;
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
    if ((id == PARAM_MIDI_PROGRAM)
            || (mod_destination_control_is_internal_lfo(id) != 0U))
        return 0U;
    if (param_registry_is_plockable(id) == 0U) return 0U;
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
        const track_runtime_type_t runtime_type =
            track_runtime_type_from_ui(config.type);
        if (tone_param_catalog_is_user_visible(runtime_type, id) == 0U)
            return 0U;
        const uint8_t set_id = ((id >= PARAM_FM_OPERATOR_FIRST)
                && (id <= PARAM_FM_OPERATOR_LAST))
            ? (uint8_t)SEQ_PLOCK_SET_FM_OPERATOR
            : (uint8_t)SEQ_PLOCK_SET_TONE;
        seq_param_slot_t slot = 0U;
        return seq_param_iface_param_to_slot_for_type((uint8_t)runtime_type,
            (uint8_t)(target_topology.role == ENTITY_ROLE_GROUP_MASTER),
            set_id, id, &slot);
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
            || (id == PARAM_ENV3_RELEASE));
    if (rule.resource == TRACK_RUNTIME_RESOURCE_FILTER)
        return (uint8_t)((config.family == TRACK_FAMILY_SYNTH)
            || (config.family == TRACK_FAMILY_SAMPLER)
            || (config.family == TRACK_FAMILY_DRUM)
            || (config.family == TRACK_FAMILY_EXTERNAL));
    return 1U;
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
    uint8_t fx_slot = 0U;
    uint8_t fx_param = 0U;
    if (audio_fx_param_catalog_param_info(param, &fx_slot, &fx_param) != 0U)
    {
        float model = 0.0f;
        if ((audio_fx_control_state_get_param(*out_target,
                (fx_slot != 0U) ? PARAM_AUDIO_FX_B_MODEL
                                : PARAM_AUDIO_FX_MODEL, &model) == 0U)
                || (audio_fx_param_catalog_resolve(
                    (uint8_t)(model + 0.5f), fx_param, out_name) == 0U))
            return 0U;
        return 1U;
    }
    if ((param >= PARAM_DRUM_MD_P1) && (param <= PARAM_DRUM_MD_P8))
    {
        float model = 0.0f;
        if (param_registry_get_track_value(
                PARAM_DRUM_MD_MODEL, *out_target, &model) == 0U) return 0U;
        const md_model_profile_t *const profile =
            md_model_profile_get(md_model_validate(model));
        const uint8_t slot = (uint8_t)(param - PARAM_DRUM_MD_P1);
        if (slot >= profile->slot_count) return 0U;
        *out_name = profile->slot_labels[slot];
        return (*out_name != NULL) ? 1U : 0U;
    }
    if (param_prism_label_for_track_param(*out_target, param, out_name) != 0U)
        return 1U;
    if (param_stack_label_for_track_param(*out_target, param, out_name) != 0U)
        return 1U;
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
