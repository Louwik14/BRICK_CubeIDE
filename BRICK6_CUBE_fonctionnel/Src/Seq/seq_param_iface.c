#include "Seq/seq_param_iface.h"

#include <string.h>

#include "Storage/memory_layout.h"

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
