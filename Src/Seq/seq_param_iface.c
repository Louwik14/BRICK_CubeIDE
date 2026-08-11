/*
 * Module: seq_param_iface
 * Role: Interface de binding entre paramètres globaux et domaines plock séquenceur.
 * Responsibilities: valider mapping set/param, maintenir base/runtime values,
 * appliquer/restaurer locks par track et exposer une API stable aux autres modules Seq.
 * Integration: couche d'abstraction entre seq_model et track_runtime/param_registry.
 */
#include "Seq/seq_param_iface.h"
#include "Seq/seq_lane.h"
#include "Seq/seq_model.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Core/track_runtime.h"
#include "Mod/mod_matrix.h"
#include "param_registry.h"
#include "NoteFx/note_fx_pipeline.h"
#include "NoteFx/note_fx_state.h"

typedef struct
{
    seq_value16_t base_value;
    seq_value16_t runtime_value;
} seq_param_slot_state_t;

SEQ_STATE_D2 static seq_param_slot_state_t
    g_seq_param_runtime_state[SEQ_LANE_CAPACITY][SEQ_PARAM_RUNTIME_SLOT_COUNT];
SEQ_STATE_D2 static uint8_t g_seq_param_base_valid_bits[SEQ_PARAM_RUNTIME_FLAG_BYTE_COUNT];
SEQ_STATE_D2 static uint8_t g_seq_param_runtime_locked_bits[SEQ_PARAM_RUNTIME_FLAG_BYTE_COUNT];
SEQ_STATE_D2 static seq_value16_t
    g_seq_play_base_values[SEQ_LANE_CAPACITY][SEQ_STEP_PLAY_VOICE_COUNT][SEQ_STEP_PLAY_FIELD_COUNT];
SEQ_STATE_D2 static uint8_t
    g_seq_play_base_valid[SEQ_LANE_CAPACITY][SEQ_STEP_PLAY_VOICE_COUNT];
#define SEQ_PARAM_SLOT_UNMAPPED ((seq_param_slot_t)0xFFU)

typedef struct
{
    uint8_t set_id;
    seq_param_slot_t param_slot;
} seq_param_compact_map_t;

static const param_id_t g_seq_param_env_slot_to_id[SEQ_PARAM_ENV_SLOT_COUNT] = {
    PARAM_FILTER_TYPE,
    PARAM_FILTER_CUTOFF,
    PARAM_FILTER_RESONANCE,
    PARAM_FILTER_EG_AMT,
    PARAM_FILTER_ATTACK,
    PARAM_FILTER_DECAY,
    PARAM_FILTER_SUSTAIN,
    PARAM_FILTER_RELEASE,
    PARAM_FILTER_KEYTRK,
    PARAM_FILTER_ENVRST,
    PARAM_FILTER_ENVDLY,
    PARAM_FILTER_EQ_LOW,
    PARAM_FILTER_EQ_MID,
    PARAM_FILTER_EQ_HIGH,
    PARAM_VCA_ATTACK,
    PARAM_VCA_DECAY,
    PARAM_VCA_SUSTAIN,
    PARAM_VCA_RELEASE,
    PARAM_ENV3_ATTACK,
    PARAM_ENV3_DECAY,
    PARAM_ENV3_SUSTAIN,
    PARAM_ENV3_RELEASE,
    PARAM_ENV_RETRIG_FILTER,
    PARAM_ENV_RETRIG_VCA,
    PARAM_ENV_RETRIG_MOD
};

static const param_id_t g_seq_param_mod_slot_to_id[SEQ_PARAM_MOD_SLOT_COUNT] = {
    PARAM_LFO1_RATE,
    PARAM_LFO1_SHAPE,
    PARAM_LFO1_TRIG,
    PARAM_LFO1_PHASE,
    PARAM_LFO2_RATE,
    PARAM_LFO2_SHAPE,
    PARAM_LFO2_TRIG,
    PARAM_LFO2_PHASE,
    PARAM_LFO3_RATE,
    PARAM_LFO3_SHAPE,
    PARAM_LFO3_TRIG,
    PARAM_LFO3_PHASE
};

static const param_id_t g_seq_param_midi_fx_slot_to_id[SEQ_PARAM_MIDI_FX_SLOT_COUNT] = {
    PARAM_MIDI_FX_S1_PARAM1,
    PARAM_MIDI_FX_S1_PARAM2,
    PARAM_MIDI_FX_S1_PARAM3,
    PARAM_MIDI_FX_S1_MODEL,
    PARAM_MIDI_FX_S2_PARAM1,
    PARAM_MIDI_FX_S2_PARAM2,
    PARAM_MIDI_FX_S2_PARAM3,
    PARAM_MIDI_FX_S2_MODEL,
    PARAM_MIDI_FX_S3_PARAM1,
    PARAM_MIDI_FX_S3_PARAM2,
    PARAM_MIDI_FX_S3_PARAM3,
    PARAM_MIDI_FX_S3_MODEL,
};

static const param_id_t g_seq_param_mix_slot_to_id[SEQ_PARAM_MIX_SLOT_COUNT] = {
    PARAM_MIX_LEVEL,
    PARAM_MIX_PAN,
    PARAM_MIX_SEND1,
    PARAM_MIX_SEND2
};

static const seq_param_compact_map_t g_seq_param_param_to_slot[PARAM_COUNT] = {
    [0 ... (PARAM_COUNT - 1U)] = { (uint8_t)SEQ_PLOCK_SET_COUNT, SEQ_PARAM_SLOT_UNMAPPED },
    [PARAM_FILTER_TYPE] = { (uint8_t)SEQ_PLOCK_SET_ENV, 0U },
    [PARAM_FILTER_CUTOFF] = { (uint8_t)SEQ_PLOCK_SET_ENV, 1U },
    [PARAM_FILTER_RESONANCE] = { (uint8_t)SEQ_PLOCK_SET_ENV, 2U },
    [PARAM_FILTER_EG_AMT] = { (uint8_t)SEQ_PLOCK_SET_ENV, 3U },
    [PARAM_FILTER_ATTACK] = { (uint8_t)SEQ_PLOCK_SET_ENV, 4U },
    [PARAM_FILTER_DECAY] = { (uint8_t)SEQ_PLOCK_SET_ENV, 5U },
    [PARAM_FILTER_SUSTAIN] = { (uint8_t)SEQ_PLOCK_SET_ENV, 6U },
    [PARAM_FILTER_RELEASE] = { (uint8_t)SEQ_PLOCK_SET_ENV, 7U },
    [PARAM_FILTER_KEYTRK] = { (uint8_t)SEQ_PLOCK_SET_ENV, 8U },
    [PARAM_FILTER_ENVRST] = { (uint8_t)SEQ_PLOCK_SET_ENV, 9U },
    [PARAM_FILTER_ENVDLY] = { (uint8_t)SEQ_PLOCK_SET_ENV, 10U },
    [PARAM_FILTER_EQ_LOW] = { (uint8_t)SEQ_PLOCK_SET_ENV, 11U },
    [PARAM_FILTER_EQ_MID] = { (uint8_t)SEQ_PLOCK_SET_ENV, 12U },
    [PARAM_FILTER_EQ_HIGH] = { (uint8_t)SEQ_PLOCK_SET_ENV, 13U },
    [PARAM_VCA_ATTACK] = { (uint8_t)SEQ_PLOCK_SET_ENV, 14U },
    [PARAM_VCA_DECAY] = { (uint8_t)SEQ_PLOCK_SET_ENV, 15U },
    [PARAM_VCA_SUSTAIN] = { (uint8_t)SEQ_PLOCK_SET_ENV, 16U },
    [PARAM_VCA_RELEASE] = { (uint8_t)SEQ_PLOCK_SET_ENV, 17U },
    [PARAM_ENV3_ATTACK] = { (uint8_t)SEQ_PLOCK_SET_ENV, 18U },
    [PARAM_ENV3_DECAY] = { (uint8_t)SEQ_PLOCK_SET_ENV, 19U },
    [PARAM_ENV3_SUSTAIN] = { (uint8_t)SEQ_PLOCK_SET_ENV, 20U },
    [PARAM_ENV3_RELEASE] = { (uint8_t)SEQ_PLOCK_SET_ENV, 21U },
    [PARAM_ENV_RETRIG_FILTER] = { (uint8_t)SEQ_PLOCK_SET_ENV, 22U },
    [PARAM_ENV_RETRIG_VCA] = { (uint8_t)SEQ_PLOCK_SET_ENV, 23U },
    [PARAM_ENV_RETRIG_MOD] = { (uint8_t)SEQ_PLOCK_SET_ENV, 24U },
    [PARAM_LFO1_RATE] = { (uint8_t)SEQ_PLOCK_SET_MOD, 0U },
    [PARAM_LFO1_SHAPE] = { (uint8_t)SEQ_PLOCK_SET_MOD, 1U },
    [PARAM_LFO1_TRIG] = { (uint8_t)SEQ_PLOCK_SET_MOD, 2U },
    [PARAM_LFO1_PHASE] = { (uint8_t)SEQ_PLOCK_SET_MOD, 3U },
    [PARAM_LFO2_RATE] = { (uint8_t)SEQ_PLOCK_SET_MOD, 4U },
    [PARAM_LFO2_SHAPE] = { (uint8_t)SEQ_PLOCK_SET_MOD, 5U },
    [PARAM_LFO2_TRIG] = { (uint8_t)SEQ_PLOCK_SET_MOD, 6U },
    [PARAM_LFO2_PHASE] = { (uint8_t)SEQ_PLOCK_SET_MOD, 7U },
    [PARAM_LFO3_RATE] = { (uint8_t)SEQ_PLOCK_SET_MOD, 8U },
    [PARAM_LFO3_SHAPE] = { (uint8_t)SEQ_PLOCK_SET_MOD, 9U },
    [PARAM_LFO3_TRIG] = { (uint8_t)SEQ_PLOCK_SET_MOD, 10U },
    [PARAM_LFO3_PHASE] = { (uint8_t)SEQ_PLOCK_SET_MOD, 11U },
    [PARAM_MIX_LEVEL] = { (uint8_t)SEQ_PLOCK_SET_MIX, 0U },
    [PARAM_MIX_PAN] = { (uint8_t)SEQ_PLOCK_SET_MIX, 1U },
    [PARAM_MIX_SEND1] = { (uint8_t)SEQ_PLOCK_SET_MIX, 2U },
    [PARAM_MIX_SEND2] = { (uint8_t)SEQ_PLOCK_SET_MIX, 3U },
    [PARAM_MIDI_FX_S1_PARAM1] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 0U },
    [PARAM_MIDI_FX_S1_PARAM2] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 1U },
    [PARAM_MIDI_FX_S1_PARAM3] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 2U },
    [PARAM_MIDI_FX_S1_MODEL] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 3U },
    [PARAM_MIDI_FX_S2_PARAM1] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 4U },
    [PARAM_MIDI_FX_S2_PARAM2] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 5U },
    [PARAM_MIDI_FX_S2_PARAM3] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 6U },
    [PARAM_MIDI_FX_S2_MODEL] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 7U },
    [PARAM_MIDI_FX_S3_PARAM1] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 8U },
    [PARAM_MIDI_FX_S3_PARAM2] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 9U },
    [PARAM_MIDI_FX_S3_PARAM3] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 10U },
    [PARAM_MIDI_FX_S3_MODEL] = { (uint8_t)SEQ_PLOCK_SET_MIDI_FX, 11U }
};

typedef struct
{
    const param_id_t *ids;
    uint8_t count;
} seq_param_inverse_table_t;

static const seq_param_inverse_table_t g_seq_param_inverse_tables[SEQ_PLOCK_SET_COUNT] = {
    [SEQ_PLOCK_SET_ENV] = { g_seq_param_env_slot_to_id, SEQ_PARAM_ENV_SLOT_COUNT },
    [SEQ_PLOCK_SET_TONE] = { NULL, 0U },
    [SEQ_PLOCK_SET_MOD] = { g_seq_param_mod_slot_to_id, SEQ_PARAM_MOD_SLOT_COUNT },
    [SEQ_PLOCK_SET_MIDI_FX] = { g_seq_param_midi_fx_slot_to_id, SEQ_PARAM_MIDI_FX_SLOT_COUNT },
    [SEQ_PLOCK_SET_MIX] = { g_seq_param_mix_slot_to_id, SEQ_PARAM_MIX_SLOT_COUNT }
};

_Static_assert((sizeof(g_seq_param_param_to_slot) / sizeof(g_seq_param_param_to_slot[0])) == PARAM_COUNT,
               "direct p-lock mapping size changed");
_Static_assert((sizeof(g_seq_param_env_slot_to_id) / sizeof(g_seq_param_env_slot_to_id[0])) == SEQ_PARAM_ENV_SLOT_COUNT,
               "ENV inverse p-lock mapping size changed");
_Static_assert((sizeof(g_seq_param_mod_slot_to_id) / sizeof(g_seq_param_mod_slot_to_id[0])) == SEQ_PARAM_MOD_SLOT_COUNT,
               "MOD inverse p-lock mapping size changed");
_Static_assert((sizeof(g_seq_param_midi_fx_slot_to_id) / sizeof(g_seq_param_midi_fx_slot_to_id[0])) == SEQ_PARAM_MIDI_FX_SLOT_COUNT,
               "MIDI FX inverse p-lock mapping size changed");
_Static_assert((sizeof(g_seq_param_mix_slot_to_id) / sizeof(g_seq_param_mix_slot_to_id[0])) == SEQ_PARAM_MIX_SLOT_COUNT,
               "MIX inverse p-lock mapping size changed");

static const uint8_t g_seq_param_set_offsets[SEQ_PLOCK_SET_COUNT] = {
    SEQ_PARAM_ENV_SLOT_OFFSET,
    SEQ_PARAM_TONE_SLOT_OFFSET,
    SEQ_PARAM_MOD_SLOT_OFFSET,
    SEQ_PARAM_MIDI_FX_SLOT_OFFSET,
    SEQ_PARAM_MIX_SLOT_OFFSET
};

static const uint8_t g_seq_param_set_capacities[SEQ_PLOCK_SET_COUNT] = {
    SEQ_PARAM_ENV_SLOT_COUNT,
    SEQ_PARAM_TONE_SLOT_COUNT,
    SEQ_PARAM_MOD_SLOT_COUNT,
    SEQ_PARAM_MIDI_FX_SLOT_COUNT,
    SEQ_PARAM_MIX_SLOT_COUNT
};

_Static_assert(SEQ_PARAM_RUNTIME_SLOT_COUNT <= UINT8_MAX,
               "p-lock compact key exceeds storage width");

static seq_param_slot_state_t *seq_param_iface_state_at(seq_track_id_t track,
                                                        uint8_t set_id,
                                                        seq_param_slot_t param_slot);
static uint8_t seq_param_iface_resolve_runtime_tone_type(seq_track_id_t track,
                                                         track_runtime_type_t *out_type);
static uint32_t seq_param_state_linear_index(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot)
{
    return ((uint32_t)track * SEQ_PARAM_RUNTIME_SLOT_COUNT)
        + (uint32_t)g_seq_param_set_offsets[set_id]
        + (uint32_t)param_slot;
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
    memset(g_seq_play_base_values, 0, sizeof(g_seq_play_base_values));
    memset(g_seq_play_base_valid, 0, sizeof(g_seq_play_base_valid));
    for (seq_track_id_t track = 0U; track < (seq_track_id_t)SEQ_LANE_CAPACITY; ++track)
    {
        for (uint16_t param_raw = 0U; param_raw < (uint16_t)PARAM_COUNT; ++param_raw)
        {
            const param_id_t param = (param_id_t)param_raw;
            if (seq_param_iface_is_play_param(param) == 0U)
            {
                continue;
            }
            (void)seq_param_iface_set_play_base_param(
                track, param,
                seq_param_iface_encode_param_value(param, param_registry[param].default_value));
        }
    }
}

static uint8_t seq_param_iface_track_is_valid(seq_track_id_t track)
{
    seq_lane_descriptor_t descriptor;
    return (seq_lane_get_descriptor((seq_lane_id_t)track, &descriptor) != 0U)
            && (descriptor.active != 0U);
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

static seq_param_slot_state_t *seq_param_iface_state_at(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot)
{
    if ((seq_param_iface_track_is_valid(track) == 0U)
            || (set_id >= (uint8_t)SEQ_PLOCK_SET_COUNT)
            || (param_slot >= (seq_param_slot_t)g_seq_param_set_capacities[set_id]))
    {
        return 0;
    }

    if (set_id == (uint8_t)SEQ_PLOCK_SET_TONE)
    {
        /* TONE slots are selected by the active engine. */
        track_runtime_type_t tone_type = TRACK_RUNTIME_TYPE_OTHER;
        param_id_t tone_param = PARAM_COUNT;
        if ((seq_param_iface_resolve_runtime_tone_type(track, &tone_type) == 0U)
                || (track_runtime_tone_slot_to_param(tone_type, param_slot, &tone_param) == 0U))
        {
            return 0;
        }
    }

    return &g_seq_param_runtime_state[track][g_seq_param_set_offsets[set_id] + param_slot];
}

static uint8_t seq_param_iface_is_excluded_from_plock(param_id_t param_id)
{
    switch (param_id)
    {
        case PARAM_SAMPLER_SLICE_COUNT:
        /* Looper decision for the current firmware: ARM/LEN/PLAY/XFADE are
         * p-lockable; STRETCH/PITCH/GRAIN are intentionally excluded. */
        case PARAM_LOOPER_STRETCH:
        case PARAM_LOOPER_PITCH:
        case PARAM_LOOPER_GRAIN:
        case PARAM_MOD_MATRIX_SLOT:
        case PARAM_MOD_MATRIX_SOURCE:
        case PARAM_MOD_MATRIX_DEST:
        case PARAM_MOD_MATRIX_DEPTH:
        case PARAM_MOD_MULTI_1_A:
        case PARAM_MOD_MULTI_1_B:
        case PARAM_MOD_MULTI_2_A:
        case PARAM_MOD_MULTI_2_B:
        case PARAM_MOD_SLEW_1_SOURCE:
        case PARAM_MOD_SLEW_1_AMOUNT:
        case PARAM_MOD_SLEW_2_SOURCE:
        case PARAM_MOD_SLEW_2_AMOUNT:
            return 1U;
        default:
            return 0U;
    }
}

uint8_t seq_param_iface_is_param_plockable(param_id_t param_id)
{
    if (param_id >= PARAM_COUNT)
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param_id);
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            || (seq_param_iface_is_excluded_from_plock(param_id) != 0U))
    {
        return 0U;
    }
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
            && (seq_param_iface_is_mix_param_plockable(param_id) == 0U))
    {
        return 0U;
    }

    return (uint8_t)((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
                     || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
                     || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD)
                     || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIDI_FX)
                     || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX));
}

static uint8_t seq_param_iface_param_matches_set_domain(uint8_t set_id, param_id_t param)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if (seq_param_iface_is_param_plockable(param) == 0U)
    {
        return 0U;
    }
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_ENV)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MOD)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX)
        && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIDI_FX))
    {
        return 0U;
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV) && (set_id != (uint8_t)SEQ_PLOCK_SET_ENV))
    {
        return 0U;
    }
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE) && (set_id != (uint8_t)SEQ_PLOCK_SET_TONE))
    {
        return 0U;
    }
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD) && (set_id != (uint8_t)SEQ_PLOCK_SET_MOD))
    {
        return 0U;
    }
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIDI_FX)
            && (set_id != (uint8_t)SEQ_PLOCK_SET_MIDI_FX))
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

    seq_lane_descriptor_t lane;
    if ((seq_lane_get_descriptor((seq_lane_id_t)track, &lane) == 0U)
            || (lane.active == 0U))
    {
        return 0U;
    }

    if (lane.role == SEQ_LANE_ROLE_GROUP_CHILD)
    {
        *out_type = TRACK_RUNTIME_TYPE_RAM;
        return 1U;
    }
    if (lane.role == SEQ_LANE_ROLE_GROUP_MASTER)
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

static uint8_t seq_param_iface_slot_is_storable_internal(seq_track_id_t track,
                                                         uint8_t set_id,
                                                         seq_param_slot_t param_slot)
{
    seq_lane_descriptor_t lane;
    if ((seq_lane_get_descriptor((seq_lane_id_t)track, &lane) == 0U)
            || (lane.active == 0U)
            || (seq_param_iface_is_slot_addressable(track, set_id, param_slot) == 0U))
    {
        return 0U;
    }
    return 1U;
}

static uint8_t seq_param_iface_is_group_master(seq_track_id_t track)
{
    seq_lane_descriptor_t lane;
    return (seq_lane_get_descriptor((seq_lane_id_t)track, &lane) != 0U)
            && (lane.role == SEQ_LANE_ROLE_GROUP_MASTER);
}

static uint8_t seq_param_iface_slot_is_supported_internal(
    seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot,
    uint8_t allow_refused_euclid_param)
{
    if ((seq_param_iface_track_is_valid(track) == 0U)
            || (seq_param_iface_is_set_plockable(set_id) == 0U))
    {
        return 0U;
    }

    param_id_t param = PARAM_COUNT;
    if (seq_param_iface_slot_to_param(track, set_id, param_slot, &param) == 0U)
    {
        return 0U;
    }
    if (seq_param_iface_is_group_master(track) != 0U)
    {
        return 1U;
    }
    if ((allow_refused_euclid_param == 0U)
            && (set_id == (uint8_t)SEQ_PLOCK_SET_MIDI_FX))
    {
        uint8_t fx_slot = 0U;
        uint8_t fx_param = 0U;
        float model_value = 0.0f;
        if ((note_fx_state_param_map(param, &fx_slot, &fx_param) != 0U)
                && (fx_param < 3U)
                && (note_fx_state_get_param(
                    track,
                    (param_id_t)(PARAM_MIDI_FX_S1_MODEL
                        + (fx_slot * NOTE_FX_PARAM_COUNT)),
                    &model_value) != 0U)
                && (note_fx_state_is_param_plock_allowed(
                    (uint8_t)(model_value + 0.5f), fx_param) == 0U))
        {
            return 0U;
        }
    }

    const track_runtime_param_status_t status =
        track_runtime_get_effective_param_status(track, param);
    return ((status == TRACK_RUNTIME_PARAM_ALLOWED)
            || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

void seq_param_iface_init(void)
{
    memset(&g_seq_param_runtime_state, 0, sizeof(g_seq_param_runtime_state));
    seq_param_clear_flags();
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

uint8_t seq_param_iface_address_to_key(uint8_t set_id,
                                       seq_param_slot_t param_slot,
                                       seq_plock_key_t *out_key)
{
    if ((out_key == 0)
            || (seq_param_iface_is_set_plockable(set_id) == 0U)
            || (param_slot >= (seq_param_slot_t)g_seq_param_set_capacities[set_id]))
    {
        return 0U;
    }

    *out_key = (seq_plock_key_t)(g_seq_param_set_offsets[set_id] + param_slot);
    return 1U;
}

uint8_t seq_param_iface_key_to_address(seq_plock_key_t key,
                                       uint8_t *out_set_id,
                                       seq_param_slot_t *out_param_slot)
{
    if ((out_set_id == 0) || (out_param_slot == 0)
            || (key >= (seq_plock_key_t)SEQ_PARAM_RUNTIME_SLOT_COUNT))
    {
        return 0U;
    }

    for (uint8_t set_id = 0U; set_id < (uint8_t)SEQ_PLOCK_SET_COUNT; ++set_id)
    {
        const uint8_t offset = g_seq_param_set_offsets[set_id];
        const uint8_t capacity = g_seq_param_set_capacities[set_id];
        if ((key >= offset) && (key < (seq_plock_key_t)(offset + capacity)))
        {
            *out_set_id = set_id;
            *out_param_slot = (seq_param_slot_t)(key - offset);
            return 1U;
        }
    }

    return 0U;
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
    if ((out_param_id == 0) || (seq_param_iface_track_is_valid(track) == 0U)
            || (seq_param_iface_is_set_plockable(set_id) == 0U))
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
    if (set_id >= (uint8_t)SEQ_PLOCK_SET_COUNT)
    {
        return 0U;
    }

    const seq_param_inverse_table_t inverse = g_seq_param_inverse_tables[set_id];
    if ((inverse.ids == NULL) || (param_slot >= inverse.count))
    {
        return 0U;
    }

    const param_id_t param = inverse.ids[param_slot];

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
    if ((out_param_slot == 0) || (seq_param_iface_track_is_valid(track) == 0U)
            || (param_id >= PARAM_COUNT))
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
    if (set_id >= (uint8_t)SEQ_PLOCK_SET_COUNT)
    {
        return 0U;
    }

    const seq_param_compact_map_t mapping = g_seq_param_param_to_slot[param_id];
    if ((mapping.set_id != set_id)
            || (mapping.param_slot >= (seq_param_slot_t)g_seq_param_set_capacities[set_id]))
    {
        return 0U;
    }

    *out_param_slot = mapping.param_slot;
    return 1U;
}

uint8_t seq_param_iface_slot_is_supported(seq_track_id_t track, uint8_t set_id, seq_param_slot_t param_slot)
{
    return seq_param_iface_slot_is_supported_internal(track, set_id,
                                                       param_slot, 0U);
}

uint8_t seq_param_iface_slot_is_storable(seq_track_id_t track,
                                         uint8_t set_id,
                                         seq_param_slot_t param_slot)
{
    return seq_param_iface_slot_is_storable_internal(track, set_id, param_slot);
}

uint8_t seq_param_iface_slot_is_storable_for_type(uint8_t runtime_type,
                                                  uint8_t set_id,
                                                  seq_param_slot_t param_slot)
{
    if (seq_param_iface_is_set_plockable(set_id) == 0U)
    {
        return 0U;
    }

    param_id_t param = PARAM_COUNT;
    if (set_id == (uint8_t)SEQ_PLOCK_SET_TONE)
    {
        if (track_runtime_tone_slot_to_param((track_runtime_type_t)runtime_type,
                                             param_slot,
                                             &param) == 0U)
        {
            return 0U;
        }
    }
    else
    {
        const seq_param_inverse_table_t inverse = g_seq_param_inverse_tables[set_id];
        if ((inverse.ids == NULL) || (param_slot >= inverse.count))
        {
            return 0U;
        }
        param = inverse.ids[param_slot];
    }

    return seq_param_iface_param_matches_set_domain(set_id, param);
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

uint8_t seq_param_iface_get_runtime_value(seq_track_id_t track,
                                          uint8_t set_id,
                                          seq_param_slot_t param_slot,
                                          seq_value16_t *out_value16)
{
    if ((out_value16 == 0)
            || (seq_param_iface_is_slot_addressable(track, set_id,
                                                     param_slot) == 0U))
    {
        return 0U;
    }

    seq_param_slot_state_t *const state =
        seq_param_iface_state_at(track, set_id, param_slot);
    if (state == 0)
    {
        return 0U;
    }
    if (seq_param_get_runtime_locked(track, set_id, param_slot) != 0U)
    {
        *out_value16 = state->runtime_value;
        return 1U;
    }
    return seq_param_iface_get_base_value(track, set_id, param_slot,
                                          out_value16);
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

uint8_t seq_param_iface_get_play_base_param(seq_track_id_t track,
                                            param_id_t param,
                                            seq_value16_t *out_value16)
{
    uint8_t voice = 0U;
    seq_step_play_field_t field = SEQ_STEP_PLAY_FIELD_NOTE;
    if ((out_value16 == NULL) || (track >= SEQ_LANE_CAPACITY)
            || (seq_model_step_play_resolve_param(param, &voice, &field) == 0U)
            || ((g_seq_play_base_valid[track][voice] & (uint8_t)(1U << field)) == 0U))
    {
        return 0U;
    }
    *out_value16 = g_seq_play_base_values[track][voice][field];
    return 1U;
}

uint8_t seq_param_iface_set_play_base_param(seq_track_id_t track,
                                            param_id_t param,
                                            seq_value16_t value16)
{
    uint8_t voice = 0U;
    seq_step_play_field_t field = SEQ_STEP_PLAY_FIELD_NOTE;
    if ((track >= SEQ_LANE_CAPACITY)
            || (seq_model_step_play_resolve_param(param, &voice, &field) == 0U))
    {
        return 0U;
    }
    g_seq_play_base_values[track][voice][field] = value16;
    g_seq_play_base_valid[track][voice] = (uint8_t)(g_seq_play_base_valid[track][voice]
                                                    | (uint8_t)(1U << field));
    return 1U;
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
    if (seq_param_iface_slot_is_supported_internal(track, set_id,
                                                    param_slot, 0U) == 0U)
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

    if (seq_param_iface_is_group_master(track) != 0U)
    {
        state->runtime_value = value16;
        seq_param_set_runtime_locked(track, set_id, param_slot, 1U);
        return 1U;
    }

    if (set_id == (uint8_t)SEQ_PLOCK_SET_MIDI_FX)
    {
        uint8_t slot = 0U, fx_param = 0U;
        if (note_fx_state_param_map(param, &slot, &fx_param) == 0U ||
            note_fx_pipeline_apply_runtime_param(track, slot, fx_param,
                (uint8_t)(seq_param_iface_decode_param_value(param, value16) + 0.5f)) == 0U)
            return 0U;
        state->runtime_value = value16;
        seq_param_set_runtime_locked(track, set_id, param_slot, 1U);
        return 1U;
    }

    const float decoded = seq_param_iface_decode_param_value(param, value16);
    mod_matrix_set_runtime_base_override(track, param, decoded);
    if (param_registry_apply_track_value_runtime_temp(param, track, decoded) == 0U)
    {
        mod_matrix_clear_runtime_base_override(
            track,
            param,
            seq_param_iface_decode_param_value(param, state->base_value));
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
    /* Restoring an already-active lock is not a new p-lock admission.  It
     * must remain possible when the target model is EUCLID and the old lock
     * was created while the slot was ARP/OFF. */
    if (seq_param_iface_slot_is_supported_internal(track, set_id,
                                                    param_slot, 1U) == 0U)
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

    if (seq_param_iface_is_group_master(track) != 0U)
    {
        state->base_value = base_value16;
        state->runtime_value = base_value16;
        seq_param_set_base_valid(track, set_id, param_slot, 1U);
        seq_param_set_runtime_locked(track, set_id, param_slot, 0U);
        return 1U;
    }

    if (set_id == (uint8_t)SEQ_PLOCK_SET_MIDI_FX)
    {
        uint8_t slot = 0U, fx_param = 0U;
        if (note_fx_state_param_map(param, &slot, &fx_param) == 0U ||
            note_fx_pipeline_release_runtime_param(track, slot, fx_param) == 0U)
            return 0U;
        state->base_value = base_value16;
        state->runtime_value = base_value16;
        seq_param_set_base_valid(track, set_id, param_slot, 1U);
        seq_param_set_runtime_locked(track, set_id, param_slot, 0U);
        return 1U;
    }

    const float decoded = seq_param_iface_decode_param_value(param, base_value16);
    if (param_registry_apply_track_value_runtime_temp(param, track, decoded) == 0U)
    {
        return 0U;
    }

    param_registry_release_track_value_runtime_temp(param, track);
    mod_matrix_clear_runtime_base_override(track, param, decoded);

    state->base_value = base_value16;
    seq_param_set_base_valid(track, set_id, param_slot, 1U);
    state->runtime_value = base_value16;
    seq_param_set_runtime_locked(track, set_id, param_slot, 0U);

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

    if ((param == PARAM_PRISM_COARSE) || (param == PARAM_PRISM_OSC2_COARSE))
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

    if ((param == PARAM_PRISM_COARSE) || (param == PARAM_PRISM_OSC2_COARSE))
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
