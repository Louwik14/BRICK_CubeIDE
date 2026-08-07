#include "NoteFx/note_fx_state.h"

#include <string.h>

#include "Core/track_topology.h"
#include "Seq/seq_lane.h"

static note_fx_track_state_t g_note_fx_state[NOTE_FX_TRACK_COUNT];

static const uint8_t g_note_fx_model_defaults[NOTE_FX_MODEL_COUNT][NOTE_FX_PARAM_COUNT] =
{
    { 2U, 0U, 1U, NOTE_FX_MODEL_OFF },
    { 2U, 0U, 1U, NOTE_FX_MODEL_ARP },
};

static uint8_t note_fx_state_default_for_model(uint8_t model, uint8_t param)
{
    if (model >= NOTE_FX_MODEL_COUNT) model = NOTE_FX_MODEL_OFF;
    return (param < NOTE_FX_PARAM_COUNT) ? g_note_fx_model_defaults[model][param] : 0U;
}

static uint8_t note_fx_state_clamp_value(uint8_t param, uint8_t value)
{
    if (param == 0U) return (value < 8U) ? value : 2U;
    if (param == 1U) return (value < 5U) ? value : 0U;
    if (param == 2U) return ((value >= 1U) && (value <= 4U)) ? value : 1U;
    if (param == 3U) return (value < NOTE_FX_MODEL_COUNT) ? value : NOTE_FX_MODEL_OFF;
    return 0U;
}

static uint8_t note_fx_state_value_is_valid(uint8_t param, uint8_t value)
{
    if (param == 0U) return (value < 8U) ? 1U : 0U;
    if (param == 1U) return (value < 5U) ? 1U : 0U;
    if (param == 2U) return ((value >= 1U) && (value <= 4U)) ? 1U : 0U;
    if (param == 3U) return (value < NOTE_FX_MODEL_COUNT) ? 1U : 0U;
    return 0U;
}

uint8_t note_fx_state_normalize_track(note_fx_track_state_t *state)
{
    if (state == 0) return 0U;

    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        uint8_t model = note_fx_state_clamp_value(3U, state->value[slot][3U]);
        state->value[slot][3U] = model;
        for (uint8_t param = 0U; param < NOTE_FX_PARAM_COUNT - 1U; ++param)
        {
            if (note_fx_state_value_is_valid(param, state->value[slot][param]) == 0U)
            {
                state->value[slot][param] = note_fx_state_default_for_model(model, param);
            }
        }
    }
    return 1U;
}

void note_fx_state_init(void)
{
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
    {
        for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
        {
            const uint8_t model = NOTE_FX_MODEL_OFF;
            for (uint8_t param = 0U; param < NOTE_FX_PARAM_COUNT; ++param)
            {
                g_note_fx_state[track].value[slot][param] = note_fx_state_default_for_model(model, param);
            }
        }
    }
}

uint8_t note_fx_state_param_map(param_id_t id, uint8_t *out_slot, uint8_t *out_param)
{
    if ((id < PARAM_MIDI_FX_S1_PARAM1) || (id > PARAM_MIDI_FX_S3_MODEL)
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

uint8_t note_fx_state_set_param(uint8_t track, param_id_t id, float value)
{
    uint8_t slot = 0U;
    uint8_t param = 0U;
    seq_lane_descriptor_t lane;
    if ((track >= NOTE_FX_TRACK_COUNT)
            || (seq_lane_get_descriptor((seq_lane_id_t)track, &lane) == 0U)
            || (lane.active == 0U)
            || (note_fx_state_param_map(id, &slot, &param) == 0U))
    {
        return 0U;
    }

    const uint8_t raw = note_fx_state_clamp_value(param, (uint8_t)(value + 0.5f));
    note_fx_track_state_t next = g_note_fx_state[track];
    if ((param == 3U) && (next.value[slot][3U] != raw))
    {
        for (uint8_t index = 0U; index < NOTE_FX_PARAM_COUNT; ++index)
        {
            next.value[slot][index] = note_fx_state_default_for_model(raw, index);
        }
    }
    else
    {
        next.value[slot][param] = raw;
    }
    (void)note_fx_state_normalize_track(&next);
    g_note_fx_state[track] = next;
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
    note_fx_track_state_t normalized = *state;
    (void)note_fx_state_normalize_track(&normalized);

    /* A restore is also a model transition.  Do not let a valid payload from
     * the previous model leak into the target model: the target model owns
     * the complete default tuple.  A restore within the same model remains a
     * regular value round-trip. */
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        if (g_note_fx_state[track].value[slot][3U]
                != normalized.value[slot][3U])
        {
            const uint8_t model = normalized.value[slot][3U];
            for (uint8_t param = 0U; param < NOTE_FX_PARAM_COUNT - 1U; ++param)
                normalized.value[slot][param] =
                    note_fx_state_default_for_model(model, param);
        }
    }
    g_note_fx_state[track] = normalized;
    return 1U;
}

uint8_t note_fx_state_restore_track_exact(uint8_t track,
                                          const note_fx_track_state_t *state)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (state == 0))
    {
        return 0U;
    }
    note_fx_track_state_t normalized = *state;
    (void)note_fx_state_normalize_track(&normalized);
    g_note_fx_state[track] = normalized;
    return 1U;
}
