#include "param_registry.h"

#include "audio_float.h"
#include "fx_daisy_comp.h"
#include "mixer.h"

static float param_values[PARAM_COUNT];

static float clamp_value(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static int8_t control_float_to_slot(float v)
{
    if (v < 0.0f)
        return -1;
    return (int8_t)v;
}

static void apply_mix_track0_gain(float v) { mixer_set_track_gain(0U, v); }
static void apply_mix_track1_gain(float v) { mixer_set_track_gain(1U, v); }
static void apply_mix_track2_gain(float v) { mixer_set_track_gain(2U, v); }
static void apply_mix_track3_gain(float v) { mixer_set_track_gain(3U, v); }

static void apply_mix_track0_pan(float v) { mixer_set_track_pan(0U, v); }
static void apply_mix_track1_pan(float v) { mixer_set_track_pan(1U, v); }
static void apply_mix_track2_pan(float v) { mixer_set_track_pan(2U, v); }
static void apply_mix_track3_pan(float v) { mixer_set_track_pan(3U, v); }

static void apply_mix_track0_mute(float v) { mixer_set_track_mute(0U, (v >= 0.5f) ? 1U : 0U); }
static void apply_mix_track1_mute(float v) { mixer_set_track_mute(1U, (v >= 0.5f) ? 1U : 0U); }
static void apply_mix_track2_mute(float v) { mixer_set_track_mute(2U, (v >= 0.5f) ? 1U : 0U); }
static void apply_mix_track3_mute(float v) { mixer_set_track_mute(3U, (v >= 0.5f) ? 1U : 0U); }

static void apply_mix_track0_route(float v) { mixer_set_track_route(0U, (mixer_route_t)((uint32_t)v & 0x3U)); }
static void apply_mix_track1_route(float v) { mixer_set_track_route(1U, (mixer_route_t)((uint32_t)v & 0x3U)); }
static void apply_mix_track2_route(float v) { mixer_set_track_route(2U, (mixer_route_t)((uint32_t)v & 0x3U)); }
static void apply_mix_track3_route(float v) { mixer_set_track_route(3U, (mixer_route_t)((uint32_t)v & 0x3U)); }

static void apply_mix_track0_insert0(float v) { mixer_set_track_insert_slot(0U, 0U, control_float_to_slot(v)); }
static void apply_mix_track0_insert1(float v) { mixer_set_track_insert_slot(0U, 1U, control_float_to_slot(v)); }
static void apply_mix_track1_insert0(float v) { mixer_set_track_insert_slot(1U, 0U, control_float_to_slot(v)); }
static void apply_mix_track1_insert1(float v) { mixer_set_track_insert_slot(1U, 1U, control_float_to_slot(v)); }
static void apply_mix_track2_insert0(float v) { mixer_set_track_insert_slot(2U, 0U, control_float_to_slot(v)); }
static void apply_mix_track2_insert1(float v) { mixer_set_track_insert_slot(2U, 1U, control_float_to_slot(v)); }
static void apply_mix_track3_insert0(float v) { mixer_set_track_insert_slot(3U, 0U, control_float_to_slot(v)); }
static void apply_mix_track3_insert1(float v) { mixer_set_track_insert_slot(3U, 1U, control_float_to_slot(v)); }

static void apply_mix_track0_send0(float v) { mixer_set_track_send_level(0U, 0U, v); }
static void apply_mix_track0_send1(float v) { mixer_set_track_send_level(0U, 1U, v); }
static void apply_mix_track1_send0(float v) { mixer_set_track_send_level(1U, 0U, v); }
static void apply_mix_track1_send1(float v) { mixer_set_track_send_level(1U, 1U, v); }
static void apply_mix_track2_send0(float v) { mixer_set_track_send_level(2U, 0U, v); }
static void apply_mix_track2_send1(float v) { mixer_set_track_send_level(2U, 1U, v); }
static void apply_mix_track3_send0(float v) { mixer_set_track_send_level(3U, 0U, v); }
static void apply_mix_track3_send1(float v) { mixer_set_track_send_level(3U, 1U, v); }

static void apply_mix_send0_fx(float v) { mixer_set_send_fx_slot(0U, control_float_to_slot(v)); }
static void apply_mix_send1_fx(float v) { mixer_set_send_fx_slot(1U, control_float_to_slot(v)); }

static void apply_bus_comp_threshold(float v) { audio_float_set_bus_comp_threshold_db(v); }
static void apply_bus_comp_ratio(float v) { audio_float_set_bus_comp_ratio(v); }
static void apply_bus_comp_attack_index(float v) { audio_float_set_bus_comp_attack_index((uint8_t)v); }
static void apply_bus_comp_release_index(float v) { audio_float_set_bus_comp_release_index((uint8_t)v); }
static void apply_bus_comp_makeup(float v) { audio_float_set_bus_comp_makeup_db(v); }
static void apply_bus_comp_mix(float v) { audio_float_set_bus_comp_auto_makeup((v >= 0.5f) ? 1U : 0U); }

static void apply_daisy_threshold(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_threshold_db(comp, v);
}

static void apply_daisy_ratio(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_ratio(comp, v);
}

static void apply_daisy_attack(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_attack_s(comp, v);
}

static void apply_daisy_release(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_release_s(comp, v);
}

static void apply_daisy_makeup(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_makeup_db(comp, v);
}

static void apply_daisy_auto_makeup(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_auto_makeup(comp, (v >= 0.5f) ? 1U : 0U);
}

static void apply_daisy_mix(float v)
{
    fx_daisy_comp_t *comp = fx_daisy_comp_get_instance();
    if (comp != NULL) fx_daisy_comp_set_mix(comp, v);
}

#define PARAM_DESC(_id, _name, _type, _min, _max, _step, _default, _unit, _apply) \
    [(_id)] = {                                                                  \
        .id = (_id),                                                             \
        .name = (_name),                                                         \
        .type = (_type),                                                         \
        .min = (_min),                                                           \
        .max = (_max),                                                           \
        .step = (_step),                                                         \
        .default_value = (_default),                                             \
        .unit = (_unit),                                                         \
        .apply = (_apply),                                                       \
    }

const param_desc_t param_registry[PARAM_COUNT] = {
    PARAM_DESC(PARAM_GRAN_DENSITY, "Gran Density", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, "", NULL),
    PARAM_DESC(PARAM_GRAN_PITCH, "Gran Pitch", PARAM_TYPE_BIPOLAR, -24.0f, 24.0f, 0.1f, 0.0f, "st", NULL),
    PARAM_DESC(PARAM_GRAN_MIX, "Gran Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, "", NULL),
    PARAM_DESC(PARAM_GRAN_FREEZE, "Gran Freeze", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, "", NULL),
    PARAM_DESC(PARAM_GRAN_SPREAD, "Gran Spread", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, "", NULL),
    PARAM_DESC(PARAM_GRAN_STEREO, "Gran Stereo", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.5f, "", NULL),

    PARAM_DESC(PARAM_MIX_TRACK0_GAIN, "Track0 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", apply_mix_track0_gain),
    PARAM_DESC(PARAM_MIX_TRACK1_GAIN, "Track1 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", apply_mix_track1_gain),
    PARAM_DESC(PARAM_MIX_TRACK2_GAIN, "Track2 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", apply_mix_track2_gain),
    PARAM_DESC(PARAM_MIX_TRACK3_GAIN, "Track3 Gain", PARAM_TYPE_FLOAT, 0.0f, 2.0f, 0.01f, 1.0f, "", apply_mix_track3_gain),

    PARAM_DESC(PARAM_MIX_TRACK0_PAN, "Track0 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track0_pan),
    PARAM_DESC(PARAM_MIX_TRACK1_PAN, "Track1 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track1_pan),
    PARAM_DESC(PARAM_MIX_TRACK2_PAN, "Track2 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track2_pan),
    PARAM_DESC(PARAM_MIX_TRACK3_PAN, "Track3 Pan", PARAM_TYPE_BIPOLAR, -1.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track3_pan),

    PARAM_DESC(PARAM_MIX_TRACK0_MUTE, "Track0 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, "", apply_mix_track0_mute),
    PARAM_DESC(PARAM_MIX_TRACK1_MUTE, "Track1 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, "", apply_mix_track1_mute),
    PARAM_DESC(PARAM_MIX_TRACK2_MUTE, "Track2 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, "", apply_mix_track2_mute),
    PARAM_DESC(PARAM_MIX_TRACK3_MUTE, "Track3 Mute", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, "", apply_mix_track3_mute),

    PARAM_DESC(PARAM_MIX_TRACK0_ROUTE, "Track0 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, "", apply_mix_track0_route),
    PARAM_DESC(PARAM_MIX_TRACK1_ROUTE, "Track1 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, "", apply_mix_track1_route),
    PARAM_DESC(PARAM_MIX_TRACK2_ROUTE, "Track2 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, "", apply_mix_track2_route),
    PARAM_DESC(PARAM_MIX_TRACK3_ROUTE, "Track3 Route", PARAM_TYPE_ENUM, 0.0f, 3.0f, 1.0f, 0.0f, "", apply_mix_track3_route),

    PARAM_DESC(PARAM_MIX_TRACK0_INSERT0, "Track0 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track0_insert0),
    PARAM_DESC(PARAM_MIX_TRACK0_INSERT1, "Track0 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track0_insert1),
    PARAM_DESC(PARAM_MIX_TRACK1_INSERT0, "Track1 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track1_insert0),
    PARAM_DESC(PARAM_MIX_TRACK1_INSERT1, "Track1 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track1_insert1),
    PARAM_DESC(PARAM_MIX_TRACK2_INSERT0, "Track2 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track2_insert0),
    PARAM_DESC(PARAM_MIX_TRACK2_INSERT1, "Track2 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track2_insert1),
    PARAM_DESC(PARAM_MIX_TRACK3_INSERT0, "Track3 Insert0", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track3_insert0),
    PARAM_DESC(PARAM_MIX_TRACK3_INSERT1, "Track3 Insert1", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_track3_insert1),

    PARAM_DESC(PARAM_MIX_TRACK0_SEND0, "Track0 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track0_send0),
    PARAM_DESC(PARAM_MIX_TRACK0_SEND1, "Track0 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track0_send1),
    PARAM_DESC(PARAM_MIX_TRACK1_SEND0, "Track1 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track1_send0),
    PARAM_DESC(PARAM_MIX_TRACK1_SEND1, "Track1 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track1_send1),
    PARAM_DESC(PARAM_MIX_TRACK2_SEND0, "Track2 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track2_send0),
    PARAM_DESC(PARAM_MIX_TRACK2_SEND1, "Track2 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track2_send1),
    PARAM_DESC(PARAM_MIX_TRACK3_SEND0, "Track3 Send0", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track3_send0),
    PARAM_DESC(PARAM_MIX_TRACK3_SEND1, "Track3 Send1", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 0.0f, "", apply_mix_track3_send1),

    PARAM_DESC(PARAM_MIX_SEND0_FX, "Send0 FX", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_send0_fx),
    PARAM_DESC(PARAM_MIX_SEND1_FX, "Send1 FX", PARAM_TYPE_ENUM, -1.0f, 127.0f, 1.0f, -1.0f, "", apply_mix_send1_fx),

    PARAM_DESC(PARAM_BUS_COMP_THRESHOLD_DB, "BusComp Threshold", PARAM_TYPE_FLOAT, -60.0f, 0.0f, 0.5f, -18.0f, "dB", apply_bus_comp_threshold),
    PARAM_DESC(PARAM_BUS_COMP_RATIO, "BusComp Ratio", PARAM_TYPE_FLOAT, 1.0f, 20.0f, 0.1f, 2.0f, "", apply_bus_comp_ratio),
    PARAM_DESC(PARAM_BUS_COMP_ATTACK_INDEX, "BusComp Attack", PARAM_TYPE_ENUM, 0.0f, 8.0f, 1.0f, 0.0f, "idx", apply_bus_comp_attack_index),
    PARAM_DESC(PARAM_BUS_COMP_RELEASE_INDEX, "BusComp Release", PARAM_TYPE_ENUM, 0.0f, 8.0f, 1.0f, 0.0f, "idx", apply_bus_comp_release_index),
    PARAM_DESC(PARAM_BUS_COMP_MAKEUP_DB, "BusComp Makeup", PARAM_TYPE_FLOAT, 0.0f, 24.0f, 0.5f, 0.0f, "dB", apply_bus_comp_makeup),
    PARAM_DESC(PARAM_BUS_COMP_MIX, "BusComp Mix", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 0.0f, "", apply_bus_comp_mix),
    PARAM_DESC(PARAM_BUS_COMP_DRYWET, "BusComp DryWet", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, "", NULL),
    PARAM_DESC(PARAM_BUS_COMP_HPF_HZ, "BusComp HPF", PARAM_TYPE_FLOAT, 0.0f, 1000.0f, 1.0f, 0.0f, "Hz", NULL),

    PARAM_DESC(PARAM_DAISY_COMP_THRESHOLD_DB, "Daisy Threshold", PARAM_TYPE_FLOAT, -40.0f, 0.0f, 0.5f, -18.0f, "dB", apply_daisy_threshold),
    PARAM_DESC(PARAM_DAISY_COMP_RATIO, "Daisy Ratio", PARAM_TYPE_FLOAT, 1.0f, 20.0f, 0.1f, 2.0f, "", apply_daisy_ratio),
    PARAM_DESC(PARAM_DAISY_COMP_ATTACK_S, "Daisy Attack", PARAM_TYPE_FLOAT, 0.0001f, 0.5f, 0.0001f, 0.001f, "s", apply_daisy_attack),
    PARAM_DESC(PARAM_DAISY_COMP_RELEASE_S, "Daisy Release", PARAM_TYPE_FLOAT, 0.01f, 5.0f, 0.01f, 0.6f, "s", apply_daisy_release),
    PARAM_DESC(PARAM_DAISY_COMP_MAKEUP_DB, "Daisy Makeup", PARAM_TYPE_FLOAT, 0.0f, 24.0f, 0.5f, 0.0f, "dB", apply_daisy_makeup),
    PARAM_DESC(PARAM_DAISY_COMP_AUTO_MAKEUP, "Daisy AutoMakeup", PARAM_TYPE_BOOL, 0.0f, 1.0f, 1.0f, 1.0f, "", apply_daisy_auto_makeup),
    PARAM_DESC(PARAM_DAISY_COMP_MIX, "Daisy Mix", PARAM_TYPE_FLOAT, 0.0f, 1.0f, 0.01f, 1.0f, "", apply_daisy_mix),
};

void param_registry_init(void)
{
    for (uint32_t i = 0U; i < (uint32_t)PARAM_COUNT; i++)
    {
        param_values[i] = param_registry[i].default_value;
    }
}

float param_get(param_id_t id)
{
    if (id >= PARAM_COUNT)
        return 0.0f;

    return param_values[id];
}

void param_set(param_id_t id, float value)
{
    if (id >= PARAM_COUNT)
        return;

    const param_desc_t *desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

    param_values[id] = clamped;

    if (desc->apply != NULL)
    {
        desc->apply(clamped);
    }
}

void param_reset(param_id_t id)
{
    if (id >= PARAM_COUNT)
        return;

    param_set(id, param_registry[id].default_value);
}
