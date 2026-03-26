#include "Seq/seq_param_iface.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "param_registry.h"

typedef struct
{
    seq_value16_t base_value;
    seq_value16_t runtime_value;
    uint8_t base_valid;
    uint8_t runtime_locked;
} seq_param_slot_state_t;

SEQ_STATE_D2 static seq_param_slot_state_t g_seq_param_state[SEQ_TRACK_COUNT][(uint8_t)SEQ_PLOCK_SET_COUNT][256U];

static uint8_t seq_param_iface_track_is_valid(seq_track_id_t track)
{
    return (track < SEQ_TRACK_COUNT) ? 1U : 0U;
}

void seq_param_iface_init(void)
{
    memset(&g_seq_param_state, 0, sizeof(g_seq_param_state));
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
    (void)param8;

    if ((seq_param_iface_track_is_valid(track) == 0U) || (seq_param_iface_is_set_plockable(set_id) == 0U))
    {
        return 0U;
    }

    return 1U;
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
        state->base_value = 0U;
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
    state->base_value = base_value16;
    state->base_valid = 1U;
    state->runtime_value = base_value16;
    state->runtime_locked = 0U;

    return 1U;
}


static uint8_t seq_param_iface_param_is_colors(param_id_t param)
{
    switch (param)
    {
        case PARAM_FILTER_TYPE:
        case PARAM_FILTER_CUTOFF:
        case PARAM_FILTER_RESONANCE:
        case PARAM_FILTER_KEYTRK:
        case PARAM_FILTER_ENVRST:
        case PARAM_FILTER_ENVDLY:
        case PARAM_FILTER_DRIVE:
        case PARAM_FILTER_EQ_LOW:
        case PARAM_FILTER_EQ_MID:
        case PARAM_FILTER_EQ_HIGH:
        case PARAM_MONOB_FILTER_TYPE:
        case PARAM_MONOB_FILTER_CUTOFF:
        case PARAM_MONOB_FILTER_RESONANCE:
        case PARAM_MONOB_FILTER_EG_AMT:
        case PARAM_MONOB_FILTER_ATTACK:
        case PARAM_MONOB_FILTER_DECAY:
        case PARAM_MONOB_FILTER_SUSTAIN:
        case PARAM_MONOB_FILTER_RELEASE:
        case PARAM_MONOB_FILTER_KEYTRK:
        case PARAM_MONOB_FILTER_ENVRST:
        case PARAM_MONOB_FILTER_ENVDLY:
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t seq_param_iface_param_is_tone(param_id_t param)
{
    switch (param)
    {
        case PARAM_DX7_ALGORITHM:
        case PARAM_DX7_FEEDBACK:
        case PARAM_DX7_TRANSPOSE:
        case PARAM_DX7_LFO_SPEED:
        case PARAM_DX7_LFO_DELAY:
        case PARAM_DX7_LFO_PITCH_MOD_DEPTH:
        case PARAM_DX7_LFO_AMP_MOD_DEPTH:
        case PARAM_DX7_PITCH_BEND_RANGE:
        case PARAM_DX7_PORTAMENTO_TIME:
        case PARAM_DX7_MONO_MODE:
        case PARAM_DX7_OPERATOR_MASK:
        case PARAM_DX7_OPERATOR_1_LEVEL:
        case PARAM_DX7_OPERATOR_2_LEVEL:
        case PARAM_DX7_OPERATOR_3_LEVEL:
        case PARAM_DX7_OPERATOR_4_LEVEL:
        case PARAM_MONOB_OSC1_WAVE:
        case PARAM_MONOB_OSC2_WAVE:
        case PARAM_MONOB_OSC3_WAVE:
        case PARAM_MONOB_SUB_WAVE:
        case PARAM_MONOB_OSC1_RANGE:
        case PARAM_MONOB_OSC2_RANGE:
        case PARAM_MONOB_OSC3_RANGE:
        case PARAM_MONOB_SUB_OCTAVE:
        case PARAM_MONOB_OSC1_DETUNE:
        case PARAM_MONOB_OSC2_DETUNE:
        case PARAM_MONOB_OSC3_DETUNE:
        case PARAM_MONOB_OSC1_MIX:
        case PARAM_MONOB_OSC2_MIX:
        case PARAM_MONOB_OSC3_MIX:
        case PARAM_MONOB_SUB_MIX:
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t seq_param_iface_param_is_play(param_id_t param)
{
    switch (param)
    {
        case PARAM_SEQ_PLAY_V1_NOTE:
        case PARAM_SEQ_PLAY_V1_VEL:
        case PARAM_SEQ_PLAY_V1_LEN:
        case PARAM_SEQ_PLAY_V1_MICTIM:
        case PARAM_SEQ_PLAY_V2_NOTE:
        case PARAM_SEQ_PLAY_V2_VEL:
        case PARAM_SEQ_PLAY_V2_LEN:
        case PARAM_SEQ_PLAY_V2_MICTIM:
        case PARAM_SEQ_PLAY_V3_NOTE:
        case PARAM_SEQ_PLAY_V3_VEL:
        case PARAM_SEQ_PLAY_V3_LEN:
        case PARAM_SEQ_PLAY_V3_MICTIM:
        case PARAM_SEQ_PLAY_V4_NOTE:
        case PARAM_SEQ_PLAY_V4_VEL:
        case PARAM_SEQ_PLAY_V4_LEN:
        case PARAM_SEQ_PLAY_V4_MICTIM:
            return 1U;

        default:
            return 0U;
    }
}

uint8_t seq_param_iface_map_param(param_id_t param,
                                  uint8_t *out_set_id,
                                  seq_param8_t *out_param8)
{
    if ((out_set_id == 0) || (out_param8 == 0))
    {
        return 0U;
    }

    if (seq_param_iface_param_is_colors(param) != 0U)
    {
        *out_set_id = (uint8_t)SEQ_PLOCK_SET_COLORS;
        *out_param8 = (seq_param8_t)param;
        return 1U;
    }

    if (seq_param_iface_param_is_tone(param) != 0U)
    {
        *out_set_id = (uint8_t)SEQ_PLOCK_SET_TONE;
        *out_param8 = (seq_param8_t)param;
        return 1U;
    }

    if (seq_param_iface_param_is_play(param) != 0U)
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
