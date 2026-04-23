/*
 * Module: seq_param_iface
 * Role: Interface de binding entre paramètres globaux et domaines plock séquenceur.
 * Responsibilities: valider mapping set/param, maintenir base/runtime values,
 * appliquer/restaurer locks par track et exposer une API stable aux autres modules Seq.
 * Integration: couche d'abstraction entre seq_model et track_runtime/param_registry.
 */
#include "Seq/seq_param_iface.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Core/track_runtime.h"
#include "param_registry.h"

typedef struct
{
    seq_value16_t base_value;
    seq_value16_t runtime_value;
    uint8_t base_valid;
    uint8_t runtime_locked;
} seq_param_slot_state_t;

SEQ_STATE_D2 static seq_param_slot_state_t g_seq_param_state[SEQ_TRACK_COUNT][(uint8_t)SEQ_PLOCK_SET_COUNT][256U];

static uint8_t seq_param_iface_is_play_param(param_id_t param)
{
    return (track_runtime_get_param_rule(param).domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY) ? 1U : 0U;
}

static void seq_param_iface_seed_play_defaults(void)
{
    for (seq_track_id_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint16_t param_raw = 0U; param_raw < (uint16_t)PARAM_COUNT; ++param_raw)
        {
            const param_id_t param = (param_id_t)param_raw;
            uint8_t set_id = 0U;
            seq_param8_t param8 = 0U;
            seq_param_slot_state_t *state;

            if (seq_param_iface_is_play_param(param) == 0U)
            {
                continue;
            }
            if (seq_param_iface_map_param(param, &set_id, &param8) == 0U)
            {
                continue;
            }

            state = &g_seq_param_state[track][set_id][param8];
            if (state->base_valid == 0U)
            {
                const float default_value = param_registry[param].default_value;
                const seq_value16_t encoded = seq_param_iface_encode_param_value(param, default_value);
                state->base_value = encoded;
                state->runtime_value = encoded;
                state->base_valid = 1U;
                state->runtime_locked = 0U;
            }
        }
    }
}

static uint8_t seq_param_iface_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

uint8_t seq_param_iface_map_param(param_id_t param,
                                  uint8_t *out_set_id,
                                  seq_param8_t *out_param8);

static uint8_t seq_param_iface_is_slot_addressable(seq_track_id_t track,
                                                   uint8_t set_id,
                                                   seq_param8_t param8)
{
    if ((seq_param_iface_track_is_valid(track) == 0U) || (seq_param_iface_is_set_plockable(set_id) == 0U))
    {
        return 0U;
    }

    const param_id_t param = (param_id_t)param8;
    if ((param >= PARAM_COUNT) || (param == PARAM_SAMPLER_SLICE_COUNT))
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MOD)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_PLAY))
    {
        return 0U;
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_COLORS) && (set_id != (uint8_t)SEQ_PLOCK_SET_COLORS))
    {
        return 0U;
    }
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE) && (set_id != (uint8_t)SEQ_PLOCK_SET_TONE))
    {
        return 0U;
    }
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY) && (set_id != (uint8_t)SEQ_PLOCK_SET_PLAY))
    {
        return 0U;
    }
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD) && (set_id != (uint8_t)SEQ_PLOCK_SET_MOD))
    {
        return 0U;
    }

    return 1U;
}

void seq_param_iface_init(void)
{
    memset(&g_seq_param_state, 0, sizeof(g_seq_param_state));
    track_runtime_init();
    seq_param_iface_seed_play_defaults();
}

uint8_t seq_param_iface_is_set_plockable(uint8_t set_id)
{
    return (set_id < (uint8_t)SEQ_PLOCK_SET_COUNT) ? 1U : 0U;
}

uint8_t seq_param_iface_set_to_mask(uint8_t set_id)
{
    if ((set_id >= 8U) || (seq_param_iface_is_set_plockable(set_id) == 0U))
    {
        return 0U;
    }

    return (uint8_t)(1U << set_id);
}

uint8_t seq_param_iface_is_param_supported(seq_track_id_t track, uint8_t set_id, seq_param8_t param8)
{
    if ((seq_param_iface_track_is_valid(track) == 0U) || (seq_param_iface_is_set_plockable(set_id) == 0U))
    {
        return 0U;
    }

    const param_id_t param = (param_id_t)param8;
    if (param >= PARAM_COUNT)
    {
        return 0U;
    }
    if (param == PARAM_SAMPLER_SLICE_COUNT)
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MOD)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_PLAY))
    {
        return 0U;
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_COLORS) && (set_id != (uint8_t)SEQ_PLOCK_SET_COLORS))
    {
        return 0U;
    }
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE) && (set_id != (uint8_t)SEQ_PLOCK_SET_TONE))
    {
        return 0U;
    }
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY) && (set_id != (uint8_t)SEQ_PLOCK_SET_PLAY))
    {
        return 0U;
    }
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD) && (set_id != (uint8_t)SEQ_PLOCK_SET_MOD))
    {
        return 0U;
    }

    track_runtime_refresh_track(track);
    const track_runtime_param_status_t status = track_runtime_get_effective_param_status(track, param);
    return ((status == TRACK_RUNTIME_PARAM_ALLOWED) || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

uint8_t seq_param_iface_get_base_value(seq_track_id_t track,
                                       uint8_t set_id,
                                       seq_param8_t param8,
                                       seq_value16_t *out_value16)
{
    if ((out_value16 == 0) || (seq_param_iface_is_param_supported(track, set_id, param8) == 0U))
    {
        return 0U;
    }

    seq_param_slot_state_t *const state = &g_seq_param_state[track][set_id][param8];
    if (state->base_valid == 0U)
    {
        const param_id_t param = (param_id_t)param8;
        if ((param >= PARAM_COUNT) || (seq_param_iface_is_play_param(param) == 0U))
        {
            float runtime_value = 0.0f;
            if ((param >= PARAM_COUNT) || (param_registry_get_track_value(param, track, &runtime_value) == 0U))
            {
                return 0U;
            }

            state->base_value = seq_param_iface_encode_param_value(param, runtime_value);
            state->base_valid = 1U;
        }
        else
        {
            state->base_value = seq_param_iface_encode_param_value(param, param_registry[param].default_value);
            state->base_valid = 1U;
        }
    }

    *out_value16 = state->base_value;
    return 1U;
}

uint8_t seq_param_iface_set_base_value(seq_track_id_t track,
                                       uint8_t set_id,
                                       seq_param8_t param8,
                                       seq_value16_t value16)
{
    if (seq_param_iface_is_param_supported(track, set_id, param8) == 0U)
    {
        return 0U;
    }

    seq_param_slot_state_t *const state = &g_seq_param_state[track][set_id][param8];
    state->base_value = value16;
    state->base_valid = 1U;

    if (state->runtime_locked == 0U)
    {
        state->runtime_value = value16;
    }

    return 1U;
}

uint8_t seq_param_iface_get_play_base_value(seq_track_id_t track,
                                            seq_param8_t param8,
                                            seq_value16_t *out_value16)
{
    return seq_param_iface_get_base_value(track, (uint8_t)SEQ_PLOCK_SET_PLAY, param8, out_value16);
}

uint8_t seq_param_iface_set_play_base_value(seq_track_id_t track,
                                            seq_param8_t param8,
                                            seq_value16_t value16)
{
    return seq_param_iface_set_base_value(track, (uint8_t)SEQ_PLOCK_SET_PLAY, param8, value16);
}

uint8_t seq_param_iface_commit_base_after_authoritative_apply(const seq_param_iface_base_commit_cmd_t *cmd)
{
    if ((cmd == 0) || (cmd->authoritative_apply_done == 0U))
    {
        return 0U;
    }

    if (cmd->source != SEQ_PARAM_IFACE_COMMIT_SOURCE_UI_TRACK_EDIT)
    {
        return 0U;
    }

    if (seq_param_iface_is_slot_addressable(cmd->target_track, cmd->set_id, cmd->param8) == 0U)
    {
        return 0U;
    }

    {
        uint8_t expected_set_id = 0U;
        seq_param8_t expected_param8 = 0U;
        if (seq_param_iface_map_param((param_id_t)cmd->param8, &expected_set_id, &expected_param8) == 0U)
        {
            return 0U;
        }
        if ((expected_set_id != cmd->set_id) || (expected_param8 != cmd->param8))
        {
            return 0U;
        }
    }

    seq_param_slot_state_t *const state = &g_seq_param_state[cmd->target_track][cmd->set_id][cmd->param8];
    state->base_value = cmd->value16;
    state->base_valid = 1U;

    if (state->runtime_locked == 0U)
    {
        state->runtime_value = cmd->value16;
    }

    return 1U;
}

uint8_t seq_param_iface_apply_lock(seq_track_id_t track,
                                   uint8_t set_id,
                                   seq_param8_t param8,
                                   seq_value16_t value16)
{
    if (seq_param_iface_is_param_supported(track, set_id, param8) == 0U)
    {
        return 0U;
    }

    seq_param_slot_state_t *const state = &g_seq_param_state[track][set_id][param8];
    if (state->base_valid == 0U)
    {
        state->base_value = state->runtime_value;
        state->base_valid = 1U;
    }

    const param_id_t param = (param_id_t)param8;
    if (param >= PARAM_COUNT)
    {
        return 0U;
    }
    if (param == PARAM_SAMPLER_SLICE_COUNT)
    {
        return 0U;
    }

    if (seq_param_iface_is_play_param(param) != 0U)
    {
        if (seq_param_iface_set_base_value(track, set_id, param8, value16) == 0U)
        {
            return 0U;
        }

        state->runtime_value = value16;
        state->runtime_locked = 1U;
        return 1U;
    }

    const float decoded = seq_param_iface_decode_param_value(param, value16);
    if (param_registry_apply_track_value(param, track, decoded) == 0U)
    {
        return 0U;
    }

    state->runtime_value = value16;
    state->runtime_locked = 1U;
    return 1U;
}

uint8_t seq_param_iface_restore_base(seq_track_id_t track,
                                     uint8_t set_id,
                                     seq_param8_t param8,
                                     seq_value16_t base_value16)
{
    if (seq_param_iface_is_param_supported(track, set_id, param8) == 0U)
    {
        return 0U;
    }

    seq_param_slot_state_t *const state = &g_seq_param_state[track][set_id][param8];
    const param_id_t param = (param_id_t)param8;
    if (param >= PARAM_COUNT)
    {
        return 0U;
    }

    if (seq_param_iface_is_play_param(param) != 0U)
    {
        if (seq_param_iface_set_base_value(track, set_id, param8, base_value16) == 0U)
        {
            return 0U;
        }

        state->base_value = base_value16;
        state->base_valid = 1U;
        state->runtime_value = base_value16;
        state->runtime_locked = 0U;
        return 1U;
    }

    const float decoded = seq_param_iface_decode_param_value(param, base_value16);
    if (param_registry_apply_track_value(param, track, decoded) == 0U)
    {
        return 0U;
    }

    state->base_value = base_value16;
    state->base_valid = 1U;
    state->runtime_value = base_value16;
    state->runtime_locked = 0U;

    return 1U;
}


uint8_t seq_param_iface_map_param(param_id_t param,
                                  uint8_t *out_set_id,
                                  seq_param8_t *out_param8)
{
    if ((out_set_id == 0) || (out_param8 == 0))
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
    {
        *out_set_id = (uint8_t)SEQ_PLOCK_SET_COLORS;
        *out_param8 = (seq_param8_t)param;
        return 1U;
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
    {
        *out_set_id = (uint8_t)SEQ_PLOCK_SET_TONE;
        *out_param8 = (seq_param8_t)param;
        return 1U;
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
    {
        *out_set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
        *out_param8 = (seq_param8_t)param;
        return 1U;
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD)
    {
        *out_set_id = (uint8_t)SEQ_PLOCK_SET_MOD;
        *out_param8 = (seq_param8_t)param;
        return 1U;
    }

    return 0U;
}

seq_value16_t seq_param_iface_encode_param_value(param_id_t param, float value)
{
    if (param >= PARAM_COUNT)
    {
        return 0U;
    }

    const param_desc_t *const desc = &param_registry[param];
    float clamped = value;
    if (clamped < desc->min)
    {
        clamped = desc->min;
    }
    if (clamped > desc->max)
    {
        clamped = desc->max;
    }

    float step = desc->step;
    if (step <= 0.0f)
    {
        step = 1.0f;
    }

    const float encoded = (clamped - desc->min) / step;
    if (encoded <= 0.0f)
    {
        return 0U;
    }

    if (encoded >= 65535.0f)
    {
        return 65535U;
    }

    return (seq_value16_t)(encoded + 0.5f);
}

float seq_param_iface_decode_param_value(param_id_t param, seq_value16_t value16)
{
    if (param >= PARAM_COUNT)
    {
        return 0.0f;
    }

    const param_desc_t *const desc = &param_registry[param];
    float step = desc->step;
    if (step <= 0.0f)
    {
        step = 1.0f;
    }

    float value = desc->min + ((float)value16 * step);
    if (value < desc->min)
    {
        value = desc->min;
    }
    if (value > desc->max)
    {
        value = desc->max;
    }

    return value;
}
