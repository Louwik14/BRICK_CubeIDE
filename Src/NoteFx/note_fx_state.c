#include "NoteFx/note_fx_state.h"

#include <string.h>

#include "Track/entity_topology.h"
#include "Seq/seq_division_catalog.h"

static note_fx_track_state_t g_note_fx_state[NOTE_FX_TRACK_COUNT];

static const uint8_t g_note_fx_model_defaults[NOTE_FX_MODEL_COUNT][NOTE_FX_PARAM_COUNT] =
{
    { SEQ_DIVISION_ARP_DEFAULT_INDEX, 0U, 1U, NOTE_FX_MODEL_OFF },
    { SEQ_DIVISION_ARP_DEFAULT_INDEX, 0U, 1U, NOTE_FX_MODEL_ARP },
    {
        NOTE_FX_EUCLID_LENGTH_DEFAULT,
        NOTE_FX_EUCLID_PULSE_DEFAULT,
        SEQ_DIVISION_ARP_DEFAULT_INDEX,
        NOTE_FX_MODEL_EUCLID
    },
};

static uint8_t note_fx_state_default_for_model(uint8_t model, uint8_t param)
{
    if (model >= NOTE_FX_MODEL_COUNT) model = NOTE_FX_MODEL_OFF;
    return (param < NOTE_FX_PARAM_COUNT) ? g_note_fx_model_defaults[model][param] : 0U;
}

static uint8_t note_fx_state_round_value(float value)
{
    if (!(value > 0.0f)) return 0U;
    if (value >= 255.0f) return 255U;
    return (uint8_t)(value + 0.5f);
}

static uint8_t note_fx_state_clamp_model(uint8_t model)
{
    return (model < NOTE_FX_MODEL_COUNT) ? model : NOTE_FX_MODEL_OFF;
}

uint8_t note_fx_state_get_param_schema(uint8_t model,
                                       uint8_t param,
                                       note_fx_param_schema_t *out_schema)
{
    if ((out_schema == 0) || (param >= NOTE_FX_PARAM_COUNT))
    {
        return 0U;
    }

    model = note_fx_state_clamp_model(model);
    if (param == 3U)
    {
        out_schema->min = NOTE_FX_MODEL_OFF;
        out_schema->max = NOTE_FX_MODEL_COUNT - 1U;
        out_schema->default_value = NOTE_FX_MODEL_OFF;
        return 1U;
    }

    if (model == NOTE_FX_MODEL_EUCLID)
    {
        static const note_fx_param_schema_t euclid_schema[NOTE_FX_PARAM_COUNT - 1U] =
        {
            { NOTE_FX_EUCLID_LENGTH_MIN, NOTE_FX_EUCLID_LENGTH_MAX,
              NOTE_FX_EUCLID_LENGTH_DEFAULT },
            { 0U, NOTE_FX_EUCLID_LENGTH_MAX, NOTE_FX_EUCLID_PULSE_DEFAULT },
            { 0U, SEQ_DIVISION_ARP_COUNT - 1U, SEQ_DIVISION_ARP_DEFAULT_INDEX },
        };
        *out_schema = euclid_schema[param];
        return 1U;
    }

    static const note_fx_param_schema_t arp_schema[NOTE_FX_PARAM_COUNT - 1U] =
    {
        { 0U, 7U, 2U },
        { 0U, 4U, 0U },
        { 1U, 4U, 1U },
    };
    *out_schema = arp_schema[param];
    return 1U;
}

uint8_t note_fx_state_is_param_plock_allowed(uint8_t model, uint8_t param)
{
    model = note_fx_state_clamp_model(model);
    return (model == NOTE_FX_MODEL_EUCLID) && (param < 3U) ? 0U : 1U;
}

static uint8_t note_fx_state_clamp_param(uint8_t model,
                                         uint8_t param,
                                         uint8_t value,
                                         uint8_t length)
{
    if (param == 3U)
    {
        return note_fx_state_clamp_model(value);
    }

    note_fx_param_schema_t schema;
    (void)note_fx_state_get_param_schema(model, param, &schema);
    if (model == NOTE_FX_MODEL_EUCLID)
    {
        if (param == 0U)
        {
            return ((value >= schema.min) && (value <= schema.max))
                ? value : schema.default_value;
        }
        if (param == 1U)
        {
            return (value <= length) ? value : length;
        }
        return ((value >= schema.min) && (value <= schema.max))
            ? value : schema.default_value;
    }

    return ((value >= schema.min) && (value <= schema.max))
        ? value : schema.default_value;
}

uint8_t note_fx_state_normalize_track(note_fx_track_state_t *state)
{
    if (state == 0) return 0U;

    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        uint8_t model = note_fx_state_clamp_model(state->value[slot][3U]);
        state->value[slot][3U] = model;
        state->value[slot][0U] = note_fx_state_clamp_param(
            model, 0U, state->value[slot][0U], 0U);
        const uint8_t length = state->value[slot][0U];
        for (uint8_t param = 1U; param < NOTE_FX_PARAM_COUNT - 1U; ++param)
        {
            state->value[slot][param] = note_fx_state_clamp_param(
                model, param, state->value[slot][param], length);
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
    if ((track >= NOTE_FX_TRACK_COUNT)
            || (entity_topology_is_active((brick_entity_id_t)track) == 0U)
            || (note_fx_state_param_map(id, &slot, &param) == 0U))
    {
        return 0U;
    }

    const uint8_t raw_value = note_fx_state_round_value(value);
    note_fx_track_state_t next = g_note_fx_state[track];
    const uint8_t model = (param == 3U)
        ? note_fx_state_clamp_model(raw_value)
        : next.value[slot][3U];
    const uint8_t raw = (param == 3U)
        ? model
        : note_fx_state_clamp_param(model, param, raw_value, next.value[slot][0U]);
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
