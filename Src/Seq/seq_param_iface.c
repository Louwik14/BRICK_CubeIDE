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
} seq_param_slot_state_t;

#define SEQ_PARAM_NON_MIX_SLOT_COUNT (SEQ_TRACK_COUNT * (uint32_t)SEQ_PLOCK_SET_MIX * 256U)
#define SEQ_PARAM_MIX_SLOT_COUNT (SEQ_TRACK_COUNT * 4U)
#define SEQ_PARAM_FLAG_BIT_COUNT (SEQ_PARAM_NON_MIX_SLOT_COUNT + SEQ_PARAM_MIX_SLOT_COUNT)
#define SEQ_PARAM_FLAG_BYTE_COUNT ((SEQ_PARAM_FLAG_BIT_COUNT + 7U) / 8U)

SEQ_STATE_D2 static seq_param_slot_state_t g_seq_param_state[SEQ_TRACK_COUNT][(uint8_t)SEQ_PLOCK_SET_MIX][256U];
SEQ_STATE_D2 static seq_param_slot_state_t g_seq_param_mix_state[SEQ_TRACK_COUNT][4U];
SEQ_STATE_D2 static uint8_t g_seq_param_base_valid_bits[SEQ_PARAM_FLAG_BYTE_COUNT];
SEQ_STATE_D2 static uint8_t g_seq_param_runtime_locked_bits[SEQ_PARAM_FLAG_BYTE_COUNT];
SEQ_STATE_D2 static seq_param_slot_t g_seq_param_id_to_slot[(uint8_t)SEQ_PLOCK_SET_MIX][PARAM_COUNT];
SEQ_STATE_D2 static param_id_t g_seq_param_slot_to_id[(uint8_t)SEQ_PLOCK_SET_MIX][256U];

static const param_id_t g_seq_param_mix_slot_to_id[4U] = {
    PARAM_MIX_LEVEL,
    PARAM_MIX_PAN,
    PARAM_MIX_SEND1,
    PARAM_MIX_SEND2
};

#define SEQ_PARAM_SLOT_UNMAPPED ((seq_param_slot_t)0xFFU)
#define SEQ_PARAM_ID_UNMAPPED ((param_id_t)0xFFFFU)

static seq_param_slot_state_t *seq_param_iface_state_at(seq_track_id_t track,
                                                        uint8_t set_id,
                                                        seq_param_slot_t param_slot);
static uint32_t seq_param_state_linear_index(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot)
{
    if (set_id == (uint8_t)SEQ_PLOCK_SET_MIX)
    {
        return SEQ_PARAM_NON_MIX_SLOT_COUNT + ((uint32_t)track * 4U) + (uint32_t)param_slot;
    }

    return (((uint32_t)track * (uint32_t)SEQ_PLOCK_SET_MIX + (uint32_t)set_id) * 256U) + (uint32_t)param_slot;
}

static uint8_t seq_param_get_flag(const uint8_t *bits,
                                  seq_track_id_t track,
                                  uint8_t set_id,
                                  seq_param_slot_t param_slot)
{
    const uint32_t index = seq_param_state_linear_index(track, set_id, param_slot);
    const uint32_t byte_index = index >> 3U;
    const uint8_t bit_mask = (uint8_t)(1U << (index & 7U));
    return ((bits[byte_index] & bit_mask) != 0U) ? 1U : 0U;
}

static void seq_param_set_flag(uint8_t *bits,
                               seq_track_id_t track,
                               uint8_t set_id,
                               seq_param_slot_t param_slot,
                               uint8_t value)
{
    const uint32_t index = seq_param_state_linear_index(track, set_id, param_slot);
    const uint32_t byte_index = index >> 3U;
    const uint8_t bit_mask = (uint8_t)(1U << (index & 7U));
    if (value != 0U)
    {
        bits[byte_index] |= bit_mask;
    }
    else
    {
        bits[byte_index] &= (uint8_t)~bit_mask;
    }
}

static uint8_t seq_param_get_base_valid(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot)
{
    return seq_param_get_flag(g_seq_param_base_valid_bits, track, set_id, param_slot);
}

static void seq_param_set_base_valid(seq_track_id_t track,
                                     uint8_t set_id,
                                     seq_param_slot_t param_slot,
                                     uint8_t value)
{
    seq_param_set_flag(g_seq_param_base_valid_bits, track, set_id, param_slot, value);
}

static uint8_t seq_param_get_runtime_locked(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot)
{
    return seq_param_get_flag(g_seq_param_runtime_locked_bits, track, set_id, param_slot);
}

static void seq_param_set_runtime_locked(seq_track_id_t track,
                                         uint8_t set_id,
                                         seq_param_slot_t param_slot,
                                         uint8_t value)
{
    seq_param_set_flag(g_seq_param_runtime_locked_bits, track, set_id, param_slot, value);
}

static void seq_param_clear_flags(void)
{
    memset(&g_seq_param_base_valid_bits, 0, sizeof(g_seq_param_base_valid_bits));
    memset(&g_seq_param_runtime_locked_bits, 0, sizeof(g_seq_param_runtime_locked_bits));
}

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
            seq_param_slot_t param_slot = 0U;
            seq_param_slot_state_t *state;

            if (seq_param_iface_is_play_param(param) == 0U)
            {
                continue;
            }
            if (seq_param_iface_map_param(param, &set_id, &param_slot) == 0U)
            {
                continue;
            }

            state = seq_param_iface_state_at(track, set_id, param_slot);
            if (state == 0)
            {
                continue;
            }
            if (seq_param_get_base_valid(track, set_id, param_slot) == 0U)
            {
                const float default_value = param_registry[param].default_value;
                const seq_value16_t encoded = seq_param_iface_encode_param_value(param, default_value);
                state->base_value = encoded;
                state->runtime_value = encoded;
                seq_param_set_base_valid(track, set_id, param_slot, 1U);
                seq_param_set_runtime_locked(track, set_id, param_slot, 0U);
            }
        }
    }
}

static uint8_t seq_param_iface_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

static uint8_t seq_param_iface_is_mix_param_plockable(param_id_t param)
{
    switch (param)
    {
        case PARAM_MIX_LEVEL:
        case PARAM_MIX_PAN:
        case PARAM_MIX_SEND1:
        case PARAM_MIX_SEND2:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t seq_param_iface_mix_param_to_slot(param_id_t param, seq_param_slot_t *out_slot)
{
    if (out_slot == 0)
    {
        return 0U;
    }

    for (seq_param_slot_t slot = 0U; slot < 4U; ++slot)
    {
        if (g_seq_param_mix_slot_to_id[slot] == param)
        {
            *out_slot = slot;
            return 1U;
        }
    }

    return 0U;
}

static uint8_t seq_param_iface_mix_slot_to_param(seq_param_slot_t slot, param_id_t *out_param)
{
    if ((out_param == 0) || (slot >= 4U))
    {
        return 0U;
    }

    *out_param = g_seq_param_mix_slot_to_id[slot];
    return 1U;
}

static seq_param_slot_state_t *seq_param_iface_state_at(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot)
{
    if (seq_param_iface_track_is_valid(track) == 0U)
    {
        return 0;
    }

    if (set_id == (uint8_t)SEQ_PLOCK_SET_MIX)
    {
        return (param_slot < 4U) ? &g_seq_param_mix_state[track][param_slot] : 0;
    }

    if (set_id >= (uint8_t)SEQ_PLOCK_SET_MIX)
    {
        return 0;
    }

    return &g_seq_param_state[track][set_id][param_slot];
}

static uint8_t seq_param_iface_set_id_from_domain(track_runtime_param_domain_t domain, uint8_t *out_set_id)
{
    if (out_set_id == 0)
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
        case TRACK_RUNTIME_PARAM_DOMAIN_MIX:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_MIX;
            return 1U;
        default:
            return 0U;
    }
}

static void seq_param_iface_rebuild_slot_maps(void)
{
    memset(g_seq_param_id_to_slot, SEQ_PARAM_SLOT_UNMAPPED, sizeof(g_seq_param_id_to_slot));

    for (uint8_t set_id = 0U; set_id < (uint8_t)SEQ_PLOCK_SET_MIX; ++set_id)
    {
        for (uint16_t slot = 0U; slot < 256U; ++slot)
        {
            g_seq_param_slot_to_id[set_id][slot] = SEQ_PARAM_ID_UNMAPPED;
        }
    }

    uint16_t next_slot[(uint8_t)SEQ_PLOCK_SET_MIX] = {0U};

    for (uint16_t param_raw = 0U; param_raw < (uint16_t)PARAM_COUNT; ++param_raw)
    {
        const param_id_t param = (param_id_t)param_raw;
        if ((param == PARAM_SAMPLER_SLICE_COUNT)
                || (param == PARAM_LOOPER_ARM)
                || (param == PARAM_LOOPER_LEN)
                || (param == PARAM_LOOPER_PLAY)
                || (param == PARAM_LOOPER_STRETCH)
                || (param == PARAM_LOOPER_PITCH)
                || (param == PARAM_LOOPER_GRAIN)
                || (param == PARAM_MOD_MATRIX_SLOT)
                || (param == PARAM_MOD_MATRIX_SOURCE)
                || (param == PARAM_MOD_MATRIX_DEST)
                || (param == PARAM_MOD_MATRIX_DEPTH)
                || (param == PARAM_MOD_MULTI_1_A)
                || (param == PARAM_MOD_MULTI_1_B)
                || (param == PARAM_MOD_MULTI_2_A)
                || (param == PARAM_MOD_MULTI_2_B)
                || (param == PARAM_MOD_SLEW_1_SOURCE)
                || (param == PARAM_MOD_SLEW_1_AMOUNT)
                || (param == PARAM_MOD_SLEW_2_SOURCE)
                || (param == PARAM_MOD_SLEW_2_AMOUNT))
        {
            continue;
        }

        if (track_runtime_get_param_rule(param).domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
        {
            continue;
        }

        uint8_t set_id = 0U;
        if (seq_param_iface_set_id_from_domain(track_runtime_get_param_rule(param).domain, &set_id) == 0U)
        {
            continue;
        }

        const uint16_t slot = next_slot[set_id];
        if (slot >= 256U)
        {
            continue;
        }

        g_seq_param_id_to_slot[set_id][param] = (seq_param_slot_t)slot;
        g_seq_param_slot_to_id[set_id][slot] = param;
        next_slot[set_id] = (uint16_t)(slot + 1U);
    }
}

static uint8_t seq_param_iface_param_matches_set_domain(uint8_t set_id, param_id_t param)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if ((param == PARAM_SAMPLER_SLICE_COUNT)
            || (param == PARAM_LOOPER_STRETCH)
            || (param == PARAM_LOOPER_PITCH)
            || (param == PARAM_LOOPER_GRAIN)
                || (param == PARAM_MOD_MATRIX_SLOT)
                || (param == PARAM_MOD_MATRIX_SOURCE)
                || (param == PARAM_MOD_MATRIX_DEST)
                || (param == PARAM_MOD_MATRIX_DEPTH)
                || (param == PARAM_MOD_MULTI_1_A)
                || (param == PARAM_MOD_MULTI_1_B)
                || (param == PARAM_MOD_MULTI_2_A)
                || (param == PARAM_MOD_MULTI_2_B)
                || (param == PARAM_MOD_SLEW_1_SOURCE)
                || (param == PARAM_MOD_SLEW_1_AMOUNT)
                || (param == PARAM_MOD_SLEW_2_SOURCE)
                || (param == PARAM_MOD_SLEW_2_AMOUNT))
    {
        return 0U;
    }
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
            && (seq_param_iface_is_mix_param_plockable(param) == 0U))
    {
        return 0U;
    }
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MOD)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX)
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
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX) && (set_id != (uint8_t)SEQ_PLOCK_SET_MIX))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t seq_param_iface_resolve_runtime_tone_type(seq_track_id_t track, track_runtime_type_t *out_type)
{
    if ((out_type == NULL) || (seq_param_iface_track_is_valid(track) == 0U))
    {
        return 0U;
    }

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    *out_type = (track_runtime_type_t)ctx->type;
    return 1U;
}

static uint8_t seq_param_iface_is_slot_addressable(seq_track_id_t track,
                                                   uint8_t set_id,
                                                   seq_param_slot_t param_slot)
{
    if ((seq_param_iface_track_is_valid(track) == 0U) || (seq_param_iface_is_set_plockable(set_id) == 0U))
    {
        return 0U;
    }

    param_id_t param = PARAM_COUNT;
    if (seq_param_iface_slot_to_param(track, set_id, param_slot, &param) == 0U)
    {
        return 0U;
    }

    if (seq_param_iface_param_matches_set_domain(set_id, param) == 0U)
    {
        return 0U;
    }

    return 1U;
}

void seq_param_iface_init(void)
{
    memset(&g_seq_param_state, 0, sizeof(g_seq_param_state));
    memset(&g_seq_param_mix_state, 0, sizeof(g_seq_param_mix_state));
    seq_param_clear_flags();
    track_runtime_init();
    seq_param_iface_rebuild_slot_maps();
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

uint8_t seq_param_iface_is_param_supported(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot)
{
    return seq_param_iface_slot_is_supported(track, set_id, param_slot);
}

uint8_t seq_param_iface_slot_to_param(seq_track_id_t track,
                                      uint8_t set_id,
                                      seq_param_slot_t param_slot,
                                      param_id_t *out_param_id)
{
    if ((out_param_id == 0) || (seq_param_iface_is_set_plockable(set_id) == 0U))
    {
        return 0U;
    }

    if (set_id == (uint8_t)SEQ_PLOCK_SET_TONE)
    {
        track_runtime_type_t tone_type = TRACK_RUNTIME_TYPE_OTHER;
        if (seq_param_iface_resolve_runtime_tone_type(track, &tone_type) == 0U)
        {
            return 0U;
        }

        param_id_t tone_param = PARAM_COUNT;
        if (track_runtime_tone_slot_to_param(tone_type, param_slot, &tone_param) == 0U)
        {
            return 0U;
        }
        if ((tone_param >= PARAM_COUNT) || (seq_param_iface_param_matches_set_domain(set_id, tone_param) == 0U))
        {
            return 0U;
        }

        *out_param_id = tone_param;
        return 1U;
    }
    if (set_id == (uint8_t)SEQ_PLOCK_SET_MIX)
    {
        param_id_t mix_param = PARAM_COUNT;
        if (seq_param_iface_mix_slot_to_param(param_slot, &mix_param) == 0U)
        {
            return 0U;
        }
        if (seq_param_iface_param_matches_set_domain(set_id, mix_param) == 0U)
        {
            return 0U;
        }
        *out_param_id = mix_param;
        return 1U;
    }

    if (set_id >= (uint8_t)SEQ_PLOCK_SET_MIX)
    {
        return 0U;
    }

    const param_id_t param = g_seq_param_slot_to_id[set_id][param_slot];
    if (param >= PARAM_COUNT)
    {
        return 0U;
    }

    if (seq_param_iface_param_matches_set_domain(set_id, param) == 0U)
    {
        return 0U;
    }

    *out_param_id = param;
    return 1U;
}

uint8_t seq_param_iface_param_to_slot(seq_track_id_t track,
                                      uint8_t set_id,
                                      param_id_t param_id,
                                      seq_param_slot_t *out_param_slot)
{
    if ((out_param_slot == 0) || (param_id >= PARAM_COUNT))
    {
        return 0U;
    }

    if (seq_param_iface_param_matches_set_domain(set_id, param_id) == 0U)
    {
        return 0U;
    }

    if (set_id == (uint8_t)SEQ_PLOCK_SET_TONE)
    {
        track_runtime_type_t tone_type = TRACK_RUNTIME_TYPE_OTHER;
        if (seq_param_iface_resolve_runtime_tone_type(track, &tone_type) == 0U)
        {
            return 0U;
        }

        uint8_t tone_slot = 0U;
        if (track_runtime_tone_param_to_slot(tone_type, param_id, &tone_slot) == 0U)
        {
            return 0U;
        }

        *out_param_slot = (seq_param_slot_t)tone_slot;
        return 1U;
    }
    if (set_id == (uint8_t)SEQ_PLOCK_SET_MIX)
    {
        return seq_param_iface_mix_param_to_slot(param_id, out_param_slot);
    }


    uint8_t mapped_set_id = 0U;
    seq_param_slot_t slot = 0U;
    if (seq_param_iface_map_param(param_id, &mapped_set_id, &slot) == 0U)
    {
        return 0U;
    }
    if (mapped_set_id != set_id)
    {
        return 0U;
    }

    *out_param_slot = slot;
    return 1U;
}

uint8_t seq_param_iface_slot_is_supported(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot)
{
    if ((seq_param_iface_track_is_valid(track) == 0U) || (seq_param_iface_is_set_plockable(set_id) == 0U))
    {
        return 0U;
    }

    param_id_t param = PARAM_COUNT;
    if (seq_param_iface_slot_to_param(track, set_id, param_slot, &param) == 0U)
    {
        return 0U;
    }

    const track_runtime_param_status_t status = track_runtime_get_effective_param_status(track, param);
    return ((status == TRACK_RUNTIME_PARAM_ALLOWED) || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

uint8_t seq_param_iface_param_is_supported(seq_track_id_t track,
                                           uint8_t set_id,
                                           param_id_t param_id)
{
    seq_param_slot_t slot = 0U;
    if (seq_param_iface_param_to_slot(track, set_id, param_id, &slot) == 0U)
    {
        return 0U;
    }

    return seq_param_iface_slot_is_supported(track, set_id, slot);
}

uint8_t seq_param_iface_get_base_value(seq_track_id_t track,
                                       uint8_t set_id,
                                       seq_param_slot_t param_slot,
                                       seq_value16_t *out_value16)
{
    if ((out_value16 == 0) || (seq_param_iface_is_param_supported(track, set_id, param_slot) == 0U))
    {
        return 0U;
    }

    seq_param_slot_state_t *const state = seq_param_iface_state_at(track, set_id, param_slot);
    if (state == 0)
    {
        return 0U;
    }
    if (seq_param_get_base_valid(track, set_id, param_slot) == 0U)
    {
        param_id_t param = PARAM_COUNT;
        float value = 0.0f;
        if ((seq_param_iface_slot_to_param(track, set_id, param_slot, &param) == 0U)
                || (seq_param_iface_is_play_param(param) != 0U)
                || (param_registry_get_track_value(param, track, &value) == 0U))
        {
            return 0U;
        }

        *out_value16 = seq_param_iface_encode_param_value(param, value);
        return 1U;
    }

    *out_value16 = state->base_value;
    return 1U;
}

uint8_t seq_param_iface_set_base_value(seq_track_id_t track,
                                       uint8_t set_id,
                                       seq_param_slot_t param_slot,
                                       seq_value16_t value16)
{
    track_runtime_refresh_track(track);
    if (seq_param_iface_is_param_supported(track, set_id, param_slot) == 0U)
    {
        return 0U;
    }
    seq_param_slot_state_t *const state = seq_param_iface_state_at(track, set_id, param_slot);
    if (state == 0)
    {
        return 0U;
    }
    state->base_value = value16;
    seq_param_set_base_valid(track, set_id, param_slot, 1U);

    if (seq_param_get_runtime_locked(track, set_id, param_slot) == 0U)
    {
        state->runtime_value = value16;
    }

    return 1U;
}

uint8_t seq_param_iface_get_play_base_value(seq_track_id_t track,
                                            seq_param_slot_t param_slot,
                                            seq_value16_t *out_value16)
{
    return seq_param_iface_get_base_value(track, (uint8_t)SEQ_PLOCK_SET_PLAY, param_slot, out_value16);
}

uint8_t seq_param_iface_set_play_base_value(seq_track_id_t track,
                                            seq_param_slot_t param_slot,
                                            seq_value16_t value16)
{
    return seq_param_iface_set_base_value(track, (uint8_t)SEQ_PLOCK_SET_PLAY, param_slot, value16);
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

    if (seq_param_iface_is_slot_addressable(cmd->target_track, cmd->set_id, cmd->param_slot) == 0U)
    {
        return 0U;
    }

    param_id_t expected_param = PARAM_COUNT;
    if (seq_param_iface_slot_to_param(cmd->target_track, cmd->set_id, cmd->param_slot, &expected_param) == 0U)
    {
        return 0U;
    }
    if (expected_param >= PARAM_COUNT)
    {
        return 0U;
    }

    seq_param_slot_state_t *const state = seq_param_iface_state_at(cmd->target_track, cmd->set_id, cmd->param_slot);
    if (state == 0)
    {
        return 0U;
    }
    state->base_value = cmd->value16;
    seq_param_set_base_valid(cmd->target_track, cmd->set_id, cmd->param_slot, 1U);

    if (seq_param_get_runtime_locked(cmd->target_track, cmd->set_id, cmd->param_slot) == 0U)
    {
        state->runtime_value = cmd->value16;
    }

    return 1U;
}

uint8_t seq_param_iface_apply_lock(seq_track_id_t track,
                                   uint8_t set_id,
                                   seq_param_slot_t param_slot,
                                   seq_value16_t value16)
{
    track_runtime_refresh_track(track);
    if (seq_param_iface_is_param_supported(track, set_id, param_slot) == 0U)
    {
        return 0U;
    }
    seq_param_slot_state_t *const state = seq_param_iface_state_at(track, set_id, param_slot);
    if (state == 0)
    {
        return 0U;
    }
    param_id_t param = PARAM_COUNT;
    if (seq_param_iface_slot_to_param(track, set_id, param_slot, &param) == 0U)
    {
        return 0U;
    }

    if (seq_param_get_base_valid(track, set_id, param_slot) == 0U)
    {
        float base = 0.0f;
        if (param_registry_get_track_value(param, track, &base) == 0U)
        {
            return 0U;
        }
        state->base_value = seq_param_iface_encode_param_value(param, base);
        state->runtime_value = state->base_value;
        seq_param_set_base_valid(track, set_id, param_slot, 1U);
    }

    if (seq_param_iface_is_play_param(param) != 0U)
    {
        state->runtime_value = value16;
        seq_param_set_runtime_locked(track, set_id, param_slot, 1U);
        return 1U;
    }

    const float decoded = seq_param_iface_decode_param_value(param, value16);
    if (param_registry_apply_track_value_runtime_temp(param, track, decoded) == 0U)
    {
        return 0U;
    }

    state->runtime_value = value16;
    seq_param_set_runtime_locked(track, set_id, param_slot, 1U);
    return 1U;
}

uint8_t seq_param_iface_restore_base(seq_track_id_t track,
                                     uint8_t set_id,
                                     seq_param_slot_t param_slot,
                                     seq_value16_t base_value16)
{
    track_runtime_refresh_track(track);
    if (seq_param_iface_is_param_supported(track, set_id, param_slot) == 0U)
    {
        return 0U;
    }
    seq_param_slot_state_t *const state = seq_param_iface_state_at(track, set_id, param_slot);
    if (state == 0)
    {
        return 0U;
    }
    param_id_t param = PARAM_COUNT;
    if (seq_param_iface_slot_to_param(track, set_id, param_slot, &param) == 0U)
    {
        return 0U;
    }

    if (seq_param_iface_is_play_param(param) != 0U)
    {
        state->runtime_value = base_value16;
        seq_param_set_runtime_locked(track, set_id, param_slot, 0U);
        return 1U;
    }

    const float decoded = seq_param_iface_decode_param_value(param, base_value16);
    if (param_registry_apply_track_value_runtime_temp(param, track, decoded) == 0U)
    {
        return 0U;
    }

    param_registry_release_track_value_runtime_temp(param, track);

    state->base_value = base_value16;
    seq_param_set_base_valid(track, set_id, param_slot, 1U);
    state->runtime_value = base_value16;
    seq_param_set_runtime_locked(track, set_id, param_slot, 0U);

    return 1U;
}


uint8_t seq_param_iface_map_param(param_id_t param,
                                  uint8_t *out_set_id,
                                  seq_param_slot_t *out_param_slot)
{
    if ((out_set_id == 0) || (out_param_slot == 0))
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    uint8_t set_id = 0U;
    if (seq_param_iface_set_id_from_domain(rule.domain, &set_id) == 0U)
    {
        return 0U;
    }

    if (param >= PARAM_COUNT)
    {
        return 0U;
    }

    if (set_id == (uint8_t)SEQ_PLOCK_SET_MIX)
    {
        seq_param_slot_t mix_slot = 0U;
        if (seq_param_iface_mix_param_to_slot(param, &mix_slot) == 0U)
        {
            return 0U;
        }
        *out_set_id = set_id;
        *out_param_slot = mix_slot;
        return 1U;
    }

    if (set_id >= (uint8_t)SEQ_PLOCK_SET_MIX)
    {
        return 0U;
    }

    const seq_param_slot_t slot = g_seq_param_id_to_slot[set_id][param];
    if (slot == SEQ_PARAM_SLOT_UNMAPPED)
    {
        return 0U;
    }

    *out_set_id = set_id;
    *out_param_slot = slot;
    return 1U;
}

seq_value16_t seq_param_iface_encode_param_value(param_id_t param, float value)
{
    if (param >= PARAM_COUNT)
    {
        return 0U;
    }

    const param_desc_t *const desc = &param_registry[param];
    float clamped = value;

    if (param == PARAM_PRISM_COARSE)
    {
        if (clamped < 0.0f)
        {
            clamped = 0.0f;
        }
        if (clamped > 1.0f)
        {
            clamped = 1.0f;
        }
        return (seq_value16_t)(clamped * 4800.0f + 0.5f);
    }

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

    if (param == PARAM_PRISM_COARSE)
    {
        float value = (float)value16 / 4800.0f;
        if (value < 0.0f)
        {
            value = 0.0f;
        }
        if (value > 1.0f)
        {
            value = 1.0f;
        }
        return value;
    }

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

