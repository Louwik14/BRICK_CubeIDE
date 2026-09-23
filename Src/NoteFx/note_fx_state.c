#include "NoteFx/note_fx_state.h"

#include <string.h>

#include "Track/entity_topology.h"
#include "Seq/seq_division_catalog.h"
#include "Seq/seq_engine.h"

static note_fx_track_state_t g_note_fx_state[NOTE_FX_TRACK_COUNT];

_Static_assert((PARAM_MIDI_FX_S1_MODEL - PARAM_MIDI_FX_S1_PARAM1)
                   == NOTE_FX_MODEL_INDEX,
               "MIDI FX slot 1 parameter order changed");
_Static_assert((PARAM_MIDI_FX_S2_PARAM1 - PARAM_MIDI_FX_S1_PARAM1)
                   == NOTE_FX_VALUE_COUNT,
               "MIDI FX slot stride changed");
_Static_assert((PARAM_MIDI_FX_S3_PARAM1 - PARAM_MIDI_FX_S2_PARAM1)
                   == NOTE_FX_VALUE_COUNT,
               "MIDI FX slot stride changed");
_Static_assert((PARAM_MIDI_FX_ORDER - PARAM_MIDI_FX_S1_PARAM1)
                   == (NOTE_FX_SLOT_COUNT * NOTE_FX_VALUE_COUNT),
               "MIDI FX order parameter boundary changed");

static const uint8_t g_note_fx_model_defaults[NOTE_FX_MODEL_COUNT][NOTE_FX_VALUE_COUNT] =
{
    { SEQ_DIVISION_ARP_DEFAULT_INDEX, 0U, 1U, 0U, NOTE_FX_MODEL_OFF },
    { 0U, SEQ_DIVISION_ARP_DEFAULT_INDEX, 1U, 0U, NOTE_FX_MODEL_ARP },
    {
        NOTE_FX_EUCLID_LENGTH_DEFAULT,
        NOTE_FX_EUCLID_PULSE_DEFAULT,
        SEQ_DIVISION_ARP_DEFAULT_INDEX, 0U,
        NOTE_FX_MODEL_EUCLID
    },
    { 100U, 0U, 0U, 0U, NOTE_FX_MODEL_PROBABILITY },
    { 100U, 0U, NOTE_FX_GATE_MODE_CLIP, 0U, NOTE_FX_MODEL_GATE },
    { 0U, 0U, 0U, 3U, NOTE_FX_MODEL_VOICER },
    { 0U, 0U, NOTE_FX_SCALER_STICK_DOWN, 12U, NOTE_FX_MODEL_SCALER },
};

static uint8_t note_fx_state_default_for_model(uint8_t model, uint8_t param)
{
    if (model >= NOTE_FX_MODEL_COUNT) model = NOTE_FX_MODEL_OFF;
    return (param < NOTE_FX_VALUE_COUNT) ? g_note_fx_model_defaults[model][param] : 0U;
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
    if ((out_schema == 0) || (param >= NOTE_FX_VALUE_COUNT))
    {
        return 0U;
    }

    model = note_fx_state_clamp_model(model);
    if (param == NOTE_FX_MODEL_INDEX)
    {
        out_schema->min = NOTE_FX_MODEL_OFF;
        out_schema->max = NOTE_FX_MODEL_COUNT - 1U;
        out_schema->default_value = NOTE_FX_MODEL_OFF;
        return 1U;
    }

    if (model == NOTE_FX_MODEL_EUCLID)
    {
        static const note_fx_param_schema_t euclid_schema[NOTE_FX_PARAM_COUNT] =
        {
            { NOTE_FX_EUCLID_LENGTH_MIN, NOTE_FX_EUCLID_LENGTH_MAX,
              NOTE_FX_EUCLID_LENGTH_DEFAULT },
            { 0U, NOTE_FX_EUCLID_LENGTH_MAX, NOTE_FX_EUCLID_PULSE_DEFAULT },
            { 0U, SEQ_DIVISION_ARP_COUNT - 1U, SEQ_DIVISION_ARP_DEFAULT_INDEX },
            { 0U, 255U, 0U },
        };
        *out_schema = euclid_schema[param];
        return 1U;
    }

    static const note_fx_param_schema_t schemas[NOTE_FX_MODEL_COUNT]
        [NOTE_FX_PARAM_COUNT] =
    {
        [NOTE_FX_MODEL_OFF] = { {0U,7U,2U}, {0U,4U,0U}, {1U,4U,1U}, {0U,255U,0U} },
        [NOTE_FX_MODEL_ARP] = { {0U,4U,0U}, {0U,SEQ_DIVISION_ARP_COUNT-1U,SEQ_DIVISION_ARP_DEFAULT_INDEX}, {1U,4U,1U}, {0U,1U,0U} },
        [NOTE_FX_MODEL_PROBABILITY] = {
            {0U,100U,100U}, {0U,NOTE_FX_PROBABILITY_CONDITION_COUNT-1U,0U},
            {0U,SEQ_DIVISION_ARP_COUNT,0U}, {0U,SEQ_DIVISION_ARP_COUNT,0U} },
        [NOTE_FX_MODEL_GATE] = {
            {1U,200U,100U}, {0U,100U,0U},
            {NOTE_FX_GATE_MODE_CLIP,NOTE_FX_GATE_MODE_EXTEND,NOTE_FX_GATE_MODE_CLIP}, {0U,255U,0U} },
        [NOTE_FX_MODEL_VOICER] = {
            {0U,NOTE_FX_VOICER_TYPE_COUNT-1U,0U}, {0U,2U,0U}, {0U,3U,0U}, {1U,4U,3U} },
        [NOTE_FX_MODEL_SCALER] = {
            {0U,6U,0U}, {0U,11U,0U},
            {NOTE_FX_SCALER_STICK_DOWN,NOTE_FX_SCALER_STICK_DROP,NOTE_FX_SCALER_STICK_DOWN}, {0U,24U,12U} },
    };
    *out_schema = schemas[model][param];
    return 1U;
}

static uint8_t note_fx_state_clamp_param(uint8_t model,
                                         uint8_t param,
                                         uint8_t value,
                                         uint8_t length)
{
    if (param == NOTE_FX_MODEL_INDEX)
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
        uint8_t model = note_fx_state_clamp_model(
            state->value[slot][NOTE_FX_MODEL_INDEX]);
        state->value[slot][NOTE_FX_MODEL_INDEX] = model;
        state->value[slot][0U] = note_fx_state_clamp_param(
            model, 0U, state->value[slot][0U], 0U);
        const uint8_t length = state->value[slot][0U];
        for (uint8_t param = 1U; param < NOTE_FX_PARAM_COUNT; ++param)
        {
            state->value[slot][param] = note_fx_state_clamp_param(
                model, param, state->value[slot][param], length);
        }
    }
    if (state->order >= NOTE_FX_ORDER_COUNT) state->order = 0U;
    return 1U;
}

note_fx_family_t note_fx_state_model_family(uint8_t model)
{
    static const note_fx_family_t families[NOTE_FX_MODEL_COUNT] =
    {
        [NOTE_FX_MODEL_OFF] = NOTE_FX_FAMILY_OFF,
        [NOTE_FX_MODEL_ARP] = NOTE_FX_FAMILY_ARP,
        [NOTE_FX_MODEL_EUCLID] = NOTE_FX_FAMILY_EUCLID,
        [NOTE_FX_MODEL_PROBABILITY] = NOTE_FX_FAMILY_PROBABILITY,
        [NOTE_FX_MODEL_GATE] = NOTE_FX_FAMILY_GATE,
        [NOTE_FX_MODEL_VOICER] = NOTE_FX_FAMILY_VOICER,
        [NOTE_FX_MODEL_SCALER] = NOTE_FX_FAMILY_SCALER,
    };
    return (model < NOTE_FX_MODEL_COUNT) ? families[model]
                                         : NOTE_FX_FAMILY_OFF;
}

uint8_t note_fx_state_available_models(const note_fx_track_state_t *state,
                                       uint8_t edited_slot,
                                       uint8_t *out_models,
                                       uint8_t capacity)
{
    static const uint8_t catalog[] =
    {
        NOTE_FX_MODEL_OFF,
        NOTE_FX_MODEL_ARP,
        NOTE_FX_MODEL_EUCLID,
        NOTE_FX_MODEL_PROBABILITY,
        NOTE_FX_MODEL_GATE,
        NOTE_FX_MODEL_VOICER,
        NOTE_FX_MODEL_SCALER,
    };
    if ((state == NULL) || (out_models == NULL)
            || (edited_slot >= NOTE_FX_SLOT_COUNT)) return 0U;

    uint16_t used_families = 0U;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        if (slot == edited_slot) continue;
        const uint8_t model = state->value[slot][NOTE_FX_MODEL_INDEX];
        const note_fx_family_t family=note_fx_state_model_family(model);
        if (family != NOTE_FX_FAMILY_OFF)
            used_families = (uint16_t)(used_families | (uint16_t)(1U << family));
    }

    uint8_t count = 0U;
    for (uint8_t index = 0U; index < (uint8_t)(sizeof(catalog) / sizeof(catalog[0])); ++index)
    {
        const uint8_t model = catalog[index];
        const note_fx_family_t family=note_fx_state_model_family(model);
        if ((family != NOTE_FX_FAMILY_OFF)
                && ((used_families & (uint16_t)(1U << family)) != 0U)) continue;
        if (count < capacity) out_models[count] = model;
        ++count;
    }
    return (count <= capacity) ? count : 0U;
}

uint8_t note_fx_state_validate_unique_families(
    const note_fx_track_state_t *state)
{
    if (state == NULL) return 0U;
    uint16_t occupied = 0U;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        const uint8_t model = state->value[slot][NOTE_FX_MODEL_INDEX];
        if (model >= NOTE_FX_MODEL_COUNT) return 0U;
        const note_fx_family_t family=note_fx_state_model_family(model);
        if (family == NOTE_FX_FAMILY_OFF) continue;
        const uint16_t bit = (uint16_t)(1U << family);
        if ((occupied & bit) != 0U) return 0U;
        occupied = (uint16_t)(occupied | bit);
    }
    return 1U;
}

void note_fx_state_init(void)
{
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        note_fx_state_make_default(&g_note_fx_state[track]);
}

void note_fx_state_make_default(note_fx_track_state_t *out_state)
{
    if (out_state == NULL) return;
    out_state->order = 0U;
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
        for (uint8_t param = 0U; param < NOTE_FX_VALUE_COUNT; ++param)
            out_state->value[slot][param] =
                note_fx_state_default_for_model(NOTE_FX_MODEL_OFF, param);
}

uint8_t note_fx_state_param_map(param_id_t id, uint8_t *out_slot, uint8_t *out_param)
{
    if ((id < PARAM_MIDI_FX_S1_PARAM1) || (id > PARAM_MIDI_FX_S3_MODEL)
            || (out_slot == 0) || (out_param == 0))
    {
        return 0U;
    }

    const uint16_t offset = (uint16_t)(id - PARAM_MIDI_FX_S1_PARAM1);
    *out_slot = (uint8_t)(offset / NOTE_FX_VALUE_COUNT);
    *out_param = (uint8_t)(offset % NOTE_FX_VALUE_COUNT);
    return 1U;
}

uint8_t note_fx_state_order_map(param_id_t id)
{
    return (id == PARAM_MIDI_FX_ORDER) ? 1U : 0U;
}

uint8_t note_fx_state_get_param(uint8_t track, param_id_t id, float *out_value)
{
    uint8_t slot = 0U;
    uint8_t param = 0U;
    if ((track >= NOTE_FX_TRACK_COUNT) || (out_value == 0))
    {
        return 0U;
    }
    if (note_fx_state_order_map(id) != 0U)
        *out_value = (float)g_note_fx_state[track].order;
    else if (note_fx_state_param_map(id, &slot, &param) != 0U)
        *out_value = (float)g_note_fx_state[track].value[slot][param];
    else return 0U;
    return 1U;
}

uint8_t note_fx_state_set_param(uint8_t track, param_id_t id, float value)
{
    uint8_t slot = 0U;
    uint8_t param = 0U;
    if ((track >= NOTE_FX_TRACK_COUNT)
            || (entity_topology_is_active((brick_entity_id_t)track) == 0U)
            || ((note_fx_state_order_map(id) == 0U)
                && (note_fx_state_param_map(id, &slot, &param) == 0U)))
    {
        return 0U;
    }

    const uint8_t raw_value = note_fx_state_round_value(value);
    note_fx_track_state_t next = g_note_fx_state[track];
    if (note_fx_state_order_map(id) != 0U)
    {
        next.order = (raw_value < NOTE_FX_ORDER_COUNT) ? raw_value : 0U;
        return note_fx_state_install_prepared_track(track, &next);
    }
    const uint8_t model = (param == NOTE_FX_MODEL_INDEX)
        ? note_fx_state_clamp_model(raw_value)
        : next.value[slot][NOTE_FX_MODEL_INDEX];
    const uint8_t raw = (param == NOTE_FX_MODEL_INDEX)
        ? model
        : note_fx_state_clamp_param(model, param, raw_value, next.value[slot][0U]);
    if ((param == NOTE_FX_MODEL_INDEX)
            && (next.value[slot][NOTE_FX_MODEL_INDEX] != raw))
    {
        for (uint8_t index = 0U; index < NOTE_FX_VALUE_COUNT; ++index)
        {
            next.value[slot][index] = note_fx_state_default_for_model(raw, index);
        }
    }
    else
    {
        next.value[slot][param] = raw;
    }
    (void)note_fx_state_normalize_track(&next);
    return note_fx_state_install_prepared_track(track, &next);
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
    return note_fx_state_install_prepared_track(track, &normalized);
}

uint8_t note_fx_state_install_prepared_track(uint8_t track,
                                             const note_fx_track_state_t *state)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (state == NULL)) return 0U;
    note_fx_track_state_t normalized = *state;
    if ((note_fx_state_normalize_track(&normalized) == 0U)
            || (note_fx_state_validate_unique_families(&normalized) == 0U))
        return 0U;
    g_note_fx_state[track] = normalized;
    seq_engine_control_mark_dirty();
    return 1U;
}
