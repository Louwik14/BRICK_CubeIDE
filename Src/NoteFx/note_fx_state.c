#include "NoteFx/note_fx_state.h"

#include <string.h>

#include "Core/track_topology.h"

static note_fx_track_state_t g_note_fx_state[NOTE_FX_TRACK_COUNT];

static uint8_t note_fx_state_default_for_param(uint8_t param)
{
    static const uint8_t defaults[NOTE_FX_PARAM_COUNT] = { 2U, 0U, 1U, NOTE_FX_MODEL_OFF };
    return (param < NOTE_FX_PARAM_COUNT) ? defaults[param] : 0U;
}

static uint8_t note_fx_state_clamp_value(uint8_t param, uint8_t value)
{
    if (param == 0U) return (value < 8U) ? value : 2U;
    if (param == 1U) return (value < 5U) ? value : 0U;
    if (param == 2U) return ((value >= 1U) && (value <= 4U)) ? value : 1U;
    if (param == 3U) return (value < NOTE_FX_MODEL_COUNT) ? value : NOTE_FX_MODEL_OFF;
    return 0U;
}

void note_fx_state_init(void)
{
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
        {
            for (uint8_t param = 0U; param < NOTE_FX_PARAM_COUNT; ++param)
            {
                g_note_fx_state[track].value[slot][param] = note_fx_state_default_for_param(param);
            }
        }
    }
}

uint8_t note_fx_state_param_map(param_id_t id, uint8_t *out_slot, uint8_t *out_param)
{
    if ((id < PARAM_MIDI_FX_S1_PARAM1) || (id > PARAM_MIDI_FX_S4_MODEL)
            || (out_slot == 0) || (out_param == 0))
    {
        return 0U;
    }

    const uint16_t offset = (uint16_t)(id - PARAM_MIDI_FX_S1_PARAM1);
    *out_slot = (uint8_t)(offset / NOTE_FX_PARAM_COUNT);
    *out_param = (uint8_t)(offset % NOTE_FX_PARAM_COUNT);
    return 1U;
}

uint8_t note_fx_state_get_param(uint8_t track, param_id_t id, float *out_value)
{
    uint8_t slot = 0U;
    uint8_t param = 0U;
    if ((track >= NOTE_FX_TRACK_COUNT) || (out_value == 0)
            || (note_fx_state_param_map(id, &slot, &param) == 0U))
    {
        return 0U;
    }
    *out_value = (float)g_note_fx_state[track].value[slot][param];
    return 1U;
}

uint8_t note_fx_state_find_arp_slot(uint8_t track, uint8_t except_slot)
{
    if (track >= NOTE_FX_TRACK_COUNT)
    {
        return NOTE_FX_SLOT_NONE;
    }
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        if ((slot != except_slot) && (g_note_fx_state[track].value[slot][3] == NOTE_FX_MODEL_ARP))
        {
            return slot;
        }
    }
    return NOTE_FX_SLOT_NONE;
}

uint8_t note_fx_state_set_param(uint8_t track, param_id_t id, float value)
{
    uint8_t slot = 0U;
    uint8_t param = 0U;
    if ((track >= NOTE_FX_TRACK_COUNT) || (track_topology_is_play(track) == 0U)
            || (note_fx_state_param_map(id, &slot, &param) == 0U))
    {
        return 0U;
    }

    uint8_t raw = note_fx_state_clamp_value(param, (uint8_t)(value + 0.5f));
    if (param == 3U)
    {
        if (raw == NOTE_FX_MODEL_ARP)
        {
            const uint8_t previous = note_fx_state_find_arp_slot(track, slot);
            if (previous < NOTE_FX_SLOT_COUNT)
            {
                g_note_fx_state[track].value[previous][3] = NOTE_FX_MODEL_OFF;
            }
        }
    }
    g_note_fx_state[track].value[slot][param] = raw;
    return 1U;
}

uint8_t note_fx_state_capture_track(uint8_t track, note_fx_track_state_t *out_state)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (out_state == 0))
    {
        return 0U;
    }
    *out_state = g_note_fx_state[track];
    return 1U;
}

uint8_t note_fx_state_restore_track(uint8_t track, const note_fx_track_state_t *state)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (state == 0))
    {
        return 0U;
    }
    uint8_t arp_seen = 0U;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        for (uint8_t param = 0U; param < NOTE_FX_PARAM_COUNT; ++param)
        {
            uint8_t value = note_fx_state_clamp_value(param, state->value[slot][param]);
            if ((param == 3U) && (value == NOTE_FX_MODEL_ARP))
            {
                if (arp_seen != 0U) value = NOTE_FX_MODEL_OFF;
                else arp_seen = 1U;
            }
            g_note_fx_state[track].value[slot][param] = value;
        }
    }
    return 1U;
}
