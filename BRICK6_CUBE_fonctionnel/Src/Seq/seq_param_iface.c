/*
 * Module: seq_param_iface
 * Role: Interface de binding entre paramètres globaux et domaines plock séquenceur.
 * Responsibilities: valider mapping set/param, maintenir base/runtime values,
 * appliquer/restaurer locks par track et exposer une API stable aux autres modules Seq.
 * Integration: couche d'abstraction entre seq_model et track_runtime/param_registry.
 */
#include "Seq/seq_param_iface.h"

#include <string.h>
#include <stdio.h>

#include "Storage/memory_layout.h"
#include "Core/track_runtime.h"
#include "param_registry.h"
#include "ui_core.h"

typedef struct
{
    seq_value16_t base_value;
    seq_value16_t runtime_value;
    uint8_t base_valid;
    uint8_t runtime_locked;
} seq_param_slot_state_t;

SEQ_STATE_D2 static seq_param_slot_state_t g_seq_param_state[SEQ_TRACK_COUNT][(uint8_t)SEQ_PLOCK_SET_COUNT][256U];

#ifndef SEQ_DEBUG_TRACK_BINDING
#define SEQ_DEBUG_TRACK_BINDING 0
#endif

#if SEQ_DEBUG_TRACK_BINDING
#define SEQ_BIND_LOG(...) printf(__VA_ARGS__)
#else
#define SEQ_BIND_LOG(...) do { } while (0)
#endif

static uint8_t seq_param_iface_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

void seq_param_iface_init(void)
{
    memset(&g_seq_param_state, 0, sizeof(g_seq_param_state));
    track_runtime_init();
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

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
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
        float runtime_value = 0.0f;
        if ((param >= PARAM_COUNT) || (param_registry_get_track_value(param, track, &runtime_value) == 0U))
        {
            return 0U;
        }

        state->base_value = seq_param_iface_encode_param_value(param, runtime_value);
        state->base_valid = 1U;
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

    const float decoded = seq_param_iface_decode_param_value(param, value16);
    SEQ_BIND_LOG("[SEQ][IFACE] apply tr=%u set=%u p=%u v16=%u dec=%.3f ui_active=%u\r\n",
                 (unsigned)track,
                 (unsigned)set_id,
                 (unsigned)param8,
                 (unsigned)value16,
                 (double)decoded,
                 (unsigned)ui_get_active_track());
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
