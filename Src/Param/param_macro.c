#include "Param/param_macro.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "Param/param_filter.h"
#include "Param/param_registry_backends.h"
#include "Param/param_registry.h"
#include "Seq/seq_param_iface.h"
#include "Storage/project_v1.h"

typedef struct
{
    float amount;
    uint8_t last_valid;
    param_macro_resolution_t last_resolution[PROJECT_V1_MACRO_SLOT_COUNT];
} param_macro_pot_state_t;

static param_macro_pot_state_t g_param_macro_pots[PROJECT_V1_MACRO_PER_BANK];

static float param_macro_clamp_amount(float amount)
{
    if (amount < 0.0f)
    {
        return 0.0f;
    }

    if (amount > 1.0f)
    {
        return 1.0f;
    }

    return amount;
}

static uint8_t param_macro_plock_set_for_domain(track_runtime_param_domain_t domain, uint8_t *out_set_id)
{
    if (out_set_id == NULL)
    {
        return 0U;
    }

    switch (domain)
    {
        case TRACK_RUNTIME_PARAM_DOMAIN_COLORS:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_COLORS;
            return 1U;
        case TRACK_RUNTIME_PARAM_DOMAIN_TONE:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_TONE;
            return 1U;
        case TRACK_RUNTIME_PARAM_DOMAIN_PLAY:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
            return 1U;
        case TRACK_RUNTIME_PARAM_DOMAIN_MOD:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_MOD;
            return 1U;
        default:
            return 0U;
    }
}

void param_macro_init(void)
{
    memset(g_param_macro_pots, 0, sizeof(g_param_macro_pots));
}

float param_macro_lerp(float base_value, float scene_value, float amount)
{
    if (amount <= 0.0f)
    {
        return base_value;
    }

    if (amount >= 1.0f)
    {
        return scene_value;
    }

    return base_value + ((scene_value - base_value) * amount);
}

uint8_t param_macro_slot_target_is_supported(uint8_t track, param_id_t param)
{
    if ((track >= SEQ_TRACK_COUNT) || (param >= PARAM_COUNT))
    {
        return 0U;
    }

    {
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
        uint8_t set_id = 0U;
        if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
        {
            return 0U;
        }

        if (param_macro_plock_set_for_domain(rule.domain, &set_id) != 0U)
        {
            return seq_param_iface_param_is_supported(track, set_id, param);
        }

        if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
                || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_BUFFER))
        {
            return (track_runtime_get_effective_param_status(track, param) == TRACK_RUNTIME_PARAM_ALLOWED) ? 1U : 0U;
        }

        return 0U;
    }
}

static uint8_t param_macro_apply_preview_value(uint8_t track, param_id_t param, float value)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    track_runtime_resolved_track_t resolved;

    if (param_macro_slot_target_is_supported(track, param) == 0U)
    {
        return 0U;
    }

    if (param_filter_is_param(param) != 0U)
    {
        return param_filter_apply_value(param, track, value, 0U, 0U);
    }

    if (track_runtime_resolve_track(track, &resolved) == 0U)
    {
        return 0U;
    }

    if (resolved.descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
    {
        return 0U;
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (param_backend_track_supports_midi_tone_descriptor(&resolved.descriptor) != 0U))
    {
        if (param == PARAM_MIDI_PROGRAM)
        {
            return param_registry_apply_track_value(param, track, value);
        }

        if (param_backend_is_midi_cc_id(param) == 0U)
        {
            return 0U;
        }

        return param_backend_send_midi_cc(track, param, value);
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD))
    {
        return param_registry_apply_track_value(param, track, value);
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
    {
        return param_backend_apply_mix_track(track_runtime_get_ctx(track), track, param, value, 0U);
    }

    if (rule.resource == TRACK_RUNTIME_RESOURCE_BUFFER)
    {
        return param_backend_apply_buffer_track(track_runtime_get_ctx(track), track, param, value);
    }

    return param_backend_apply_track_value(track, param, value, 0U);
}

uint8_t param_macro_resolve_slot(uint8_t bank,
                                 uint8_t macro,
                                 uint8_t slot,
                                 param_macro_resolution_t *out_resolution);
uint8_t param_macro_apply_resolution(const param_macro_resolution_t *resolution);

static void param_macro_release_pot(uint8_t macro)
{
    if (macro >= PROJECT_V1_MACRO_PER_BANK)
    {
        return;
    }

    param_macro_pot_state_t *const pot = &g_param_macro_pots[macro];
    if (pot->last_valid == 0U)
    {
        return;
    }

    for (uint8_t slot = 0U; slot < PROJECT_V1_MACRO_SLOT_COUNT; ++slot)
    {
        const param_macro_resolution_t *const last = &pot->last_resolution[slot];
        if ((last->track >= SEQ_TRACK_COUNT)
                || (last->param >= PARAM_COUNT)
                || (last->resolved_value == last->base_value))
        {
            continue;
        }

        (void)param_macro_apply_preview_value(last->track, last->param, last->base_value);
    }

    memset(pot->last_resolution, 0, sizeof(pot->last_resolution));
    pot->last_valid = 0U;
}

static uint8_t param_macro_apply_pot(uint8_t bank, uint8_t macro, float amount)
{
    param_macro_pot_state_t *const pot = (macro < PROJECT_V1_MACRO_PER_BANK) ? &g_param_macro_pots[macro] : NULL;
    uint8_t any_applied = 0U;

    if (pot == NULL)
    {
        return 0U;
    }

    amount = param_macro_clamp_amount(amount);
    param_macro_release_pot(macro);

    pot->amount = amount;
    for (uint8_t slot = 0U; slot < PROJECT_V1_MACRO_SLOT_COUNT; ++slot)
    {
        param_macro_resolution_t resolution;
        if (param_macro_resolve_slot(bank, macro, slot, &resolution) == 0U)
        {
            continue;
        }

        resolution.amount = amount;
        resolution.resolved_value = param_macro_lerp(resolution.base_value, resolution.scene_value, amount);
        if (param_macro_apply_resolution(&resolution) == 0U)
        {
            continue;
        }

        pot->last_resolution[slot] = resolution;
        pot->last_valid = 1U;
        any_applied = 1U;
    }

    return any_applied;
}

uint8_t param_macro_resolve_slot(uint8_t bank,
                                 uint8_t macro,
                                 uint8_t slot,
                                 param_macro_resolution_t *out_resolution)
{
    project_v1_macro_slot_t macro_slot;
    float base_value = 0.0f;

    if (out_resolution == NULL)
    {
        return 0U;
    }

    memset(out_resolution, 0, sizeof(*out_resolution));

    if (project_v1_macro_get_slot(bank, macro, slot, &macro_slot) == 0U)
    {
        return 0U;
    }

    if ((macro_slot.track == PROJECT_V1_MACRO_SLOT_TRACK_NONE)
            || (macro_slot.param == PROJECT_V1_MACRO_SLOT_PARAM_NONE)
            || (param_macro_slot_target_is_supported(macro_slot.track, macro_slot.param) == 0U))
    {
        return 0U;
    }

    if (param_registry_get_track_value(macro_slot.param, macro_slot.track, &base_value) == 0U)
    {
        return 0U;
    }

    out_resolution->bank = bank;
    out_resolution->macro = macro;
    out_resolution->slot = slot;
    out_resolution->track = macro_slot.track;
    out_resolution->param = macro_slot.param;
    out_resolution->base_value = base_value;
    out_resolution->scene_value = macro_slot.scene_value;
    out_resolution->amount = 0.0f;
    out_resolution->resolved_value = base_value;
    return 1U;
}

uint8_t param_macro_apply_resolution(const param_macro_resolution_t *resolution)
{
    if ((resolution == NULL)
            || (resolution->track >= SEQ_TRACK_COUNT)
            || (resolution->param >= PARAM_COUNT)
            || (param_macro_slot_target_is_supported(resolution->track, resolution->param) == 0U))
    {
        return 0U;
    }

    return param_macro_apply_preview_value(resolution->track,
                                           resolution->param,
                                           resolution->resolved_value);
}

void param_macro_sync_active_bank(void)
{
    const uint8_t bank = project_v1_macro_get_active_bank();

    for (uint8_t macro = 0U; macro < PROJECT_V1_MACRO_PER_BANK; ++macro)
    {
        (void)param_macro_apply_pot(bank, macro, g_param_macro_pots[macro].amount);
    }
}

uint8_t param_macro_set_amount(uint8_t macro, float amount)
{
    if (macro >= PROJECT_V1_MACRO_PER_BANK)
    {
        return 0U;
    }

    return param_macro_apply_pot(project_v1_macro_get_active_bank(), macro, amount);
}

uint8_t param_macro_adjust_amount(uint8_t macro, int16_t delta)
{
    float next_amount = 0.0f;

    if (macro >= PROJECT_V1_MACRO_PER_BANK)
    {
        return 0U;
    }

    next_amount = g_param_macro_pots[macro].amount + ((float)delta * 0.015625f);
    return param_macro_set_amount(macro, next_amount);
}

float param_macro_get_amount(uint8_t macro)
{
    if (macro >= PROJECT_V1_MACRO_PER_BANK)
    {
        return 0.0f;
    }

    return g_param_macro_pots[macro].amount;
}

uint8_t param_macro_apply_slot(uint8_t bank, uint8_t macro, uint8_t slot, float amount)
{
    param_macro_resolution_t resolution;
    float clamped_amount = amount;

    if (param_macro_resolve_slot(bank, macro, slot, &resolution) == 0U)
    {
        return 0U;
    }

    if (clamped_amount < 0.0f)
    {
        clamped_amount = 0.0f;
    }
    else if (clamped_amount > 1.0f)
    {
        clamped_amount = 1.0f;
    }

    resolution.amount = clamped_amount;
    resolution.resolved_value = param_macro_lerp(resolution.base_value, resolution.scene_value, clamped_amount);
    return param_macro_apply_resolution(&resolution);
}
