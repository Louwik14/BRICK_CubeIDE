#include "NoteFx/note_fx_state.h"

#include <stddef.h>

#include "Track/entity_topology.h"
#include "Seq/seq_division_catalog.h"
#include "Seq/seq_engine.h"

static note_fx_chain_state_t g_note_fx_chain_state[NOTE_FX_TRACK_COUNT];

static uint8_t note_fx_state_round_value(float value)
{
    if (value <= 0.0f) return 0U;
    if (value >= 255.0f) return 255U;
    return (uint8_t)(value + 0.5f);
}

static uint8_t note_fx_chain_clamp(uint8_t value, uint8_t minimum,
                                   uint8_t maximum)
{
    if (value < minimum) return minimum;
    return (value > maximum) ? maximum : value;
}

void note_fx_state_init(void)
{
    for (uint8_t track = 0U; track < NOTE_FX_TRACK_COUNT; ++track)
        note_fx_chain_state_make_default(&g_note_fx_chain_state[track]);
}

void note_fx_chain_state_make_default(note_fx_chain_state_t *out_state)
{
    if (out_state == NULL) return;
    *out_state = (note_fx_chain_state_t){
        .generator = {0U, SEQ_DIVISION_ARP_DEFAULT_INDEX, 1U,
                      NOTE_FX_GENERATOR_OFF},
        .voicer = {0U, NOTE_FX_VOICER_SPREAD_CLOSED,
                   NOTE_FX_VOICER_INVERT_ROOT, NOTE_FX_VOICER_MODE_OFF},
        .scaler = {0U, NOTE_FX_SCALER_STICK_FIXED_DOWN, 12U,
                   NOTE_FX_SCALER_SCALE_OFF},
        .trig = {NOTE_FX_TRIG_LOT_GROUP, NOTE_FX_TRIG_KEEP_OFF, 100U,
                 NOTE_FX_TRIG_CHANCE_OFF}
    };
}

uint8_t note_fx_chain_state_make_effective(
    const note_fx_chain_state_t *raw_state,
    note_fx_chain_state_t *out_effective)
{
    if ((raw_state == NULL) || (out_effective == NULL)) return 0U;
    note_fx_chain_state_t effective = *raw_state;
    effective.generator.mode = note_fx_chain_clamp(
        effective.generator.mode, NOTE_FX_GENERATOR_OFF,
        NOTE_FX_GENERATOR_MODE_COUNT - 1U);
    if ((effective.generator.mode == NOTE_FX_GENERATOR_ARP)
            || (effective.generator.mode == NOTE_FX_GENERATOR_HOLD))
    {
        effective.generator.p1 = note_fx_chain_clamp(effective.generator.p1, 0U, 4U);
        effective.generator.p2 = note_fx_chain_clamp(
            effective.generator.p2, 0U, SEQ_DIVISION_ARP_COUNT - 1U);
        effective.generator.p3 = note_fx_chain_clamp(effective.generator.p3, 1U, 4U);
    }
    else if (effective.generator.mode == NOTE_FX_GENERATOR_EUCLID)
    {
        effective.generator.p1 = note_fx_chain_clamp(
            effective.generator.p1, NOTE_FX_EUCLID_LENGTH_MIN,
            NOTE_FX_EUCLID_LENGTH_MAX);
        effective.generator.p2 = note_fx_chain_clamp(
            effective.generator.p2, 0U, effective.generator.p1);
        effective.generator.p3 = note_fx_chain_clamp(
            effective.generator.p3, 0U, SEQ_DIVISION_ARP_COUNT - 1U);
    }
    effective.voicer.type = note_fx_chain_clamp(
        effective.voicer.type, 0U, NOTE_FX_VOICER_TYPE_COUNT - 1U);
    effective.voicer.spread = note_fx_chain_clamp(
        effective.voicer.spread, NOTE_FX_VOICER_SPREAD_CLOSED,
        NOTE_FX_VOICER_SPREAD_COUNT - 1U);
    effective.voicer.invert = note_fx_chain_clamp(
        effective.voicer.invert, NOTE_FX_VOICER_INVERT_ROOT,
        NOTE_FX_VOICER_INVERT_COUNT - 1U);
    effective.voicer.mode = note_fx_chain_clamp(
        effective.voicer.mode, NOTE_FX_VOICER_MODE_OFF,
        NOTE_FX_VOICER_MODE_COUNT - 1U);
    effective.scaler.key = note_fx_chain_clamp(effective.scaler.key, 0U, 11U);
    effective.scaler.stick = note_fx_chain_clamp(
        effective.scaler.stick, NOTE_FX_SCALER_STICK_FIXED_DOWN,
        NOTE_FX_SCALER_STICK_COUNT - 1U);
    effective.scaler.transpose = note_fx_chain_clamp(effective.scaler.transpose, 0U, 24U);
    effective.scaler.scale = note_fx_chain_clamp(
        effective.scaler.scale, NOTE_FX_SCALER_SCALE_OFF,
        NOTE_FX_SCALER_SCALE_COUNT - 1U);
    effective.trig.lot = note_fx_chain_clamp(
        effective.trig.lot, NOTE_FX_TRIG_LOT_GROUP,
        NOTE_FX_TRIG_LOT_DIVISION_BASE + SEQ_DIVISION_ARP_COUNT - 1U);
    effective.trig.keep = note_fx_chain_clamp(
        effective.trig.keep, NOTE_FX_TRIG_KEEP_OFF,
        NOTE_FX_TRIG_KEEP_DIVISION_BASE + SEQ_DIVISION_ARP_COUNT - 1U);
    effective.trig.gate = note_fx_chain_clamp(effective.trig.gate, 1U, 200U);
    effective.trig.chance = note_fx_chain_clamp(
        effective.trig.chance, NOTE_FX_TRIG_CHANCE_OFF, NOTE_FX_TRIG_CHANCE_0);
    *out_effective = effective;
    return 1U;
}

uint8_t note_fx_chain_param_is_plockable(note_fx_chain_stage_t stage,
                                         uint8_t param)
{
    if (((uint8_t)stage >= NOTE_FX_CHAIN_STAGE_COUNT)
            || (param >= NOTE_FX_CHAIN_PARAM_COUNT)) return 0U;
    return ((stage == NOTE_FX_CHAIN_STAGE_GENERATOR) && (param == 3U)) ? 0U : 1U;
}

uint8_t note_fx_chain_param_map(param_id_t id, note_fx_chain_stage_t *out_stage,
                                uint8_t *out_param)
{
    if ((id < PARAM_MIDI_FX_GENERATOR_P1) || (id > PARAM_MIDI_FX_TRIG_P4)
            || (out_stage == NULL) || (out_param == NULL)) return 0U;
    const uint16_t offset = (uint16_t)(id - PARAM_MIDI_FX_GENERATOR_P1);
    *out_stage = (note_fx_chain_stage_t)(offset / NOTE_FX_CHAIN_PARAM_COUNT);
    *out_param = (uint8_t)(offset % NOTE_FX_CHAIN_PARAM_COUNT);
    return 1U;
}

static uint8_t *note_fx_chain_value(note_fx_chain_state_t *state,
                                    note_fx_chain_stage_t stage)
{
    return ((uint8_t *)state) + ((uint8_t)stage * NOTE_FX_CHAIN_PARAM_COUNT);
}

uint8_t note_fx_chain_state_get_param(uint8_t track, param_id_t id,
                                      float *out_value)
{
    note_fx_chain_stage_t stage;
    uint8_t param;
    if ((track >= NOTE_FX_TRACK_COUNT) || (out_value == NULL)
            || (note_fx_chain_param_map(id, &stage, &param) == 0U)) return 0U;
    *out_value = (float)note_fx_chain_value(&g_note_fx_chain_state[track], stage)[param];
    return 1U;
}

uint8_t note_fx_chain_state_set_param(uint8_t track, param_id_t id, float value)
{
    note_fx_chain_stage_t stage;
    uint8_t param;
    if ((track >= NOTE_FX_TRACK_COUNT)
            || (entity_topology_is_active((brick_entity_id_t)track) == 0U)
            || (note_fx_chain_param_map(id, &stage, &param) == 0U)) return 0U;
    note_fx_chain_value(&g_note_fx_chain_state[track], stage)[param] =
        note_fx_state_round_value(value);
    seq_engine_control_mark_dirty();
    return 1U;
}

uint8_t note_fx_chain_state_capture_track(uint8_t track,
                                          note_fx_chain_state_t *out_state)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (out_state == NULL)) return 0U;
    *out_state = g_note_fx_chain_state[track];
    return 1U;
}

uint8_t note_fx_chain_state_install_track(uint8_t track,
                                          const note_fx_chain_state_t *state)
{
    if ((track >= NOTE_FX_TRACK_COUNT) || (state == NULL)) return 0U;
    g_note_fx_chain_state[track] = *state;
    seq_engine_control_mark_dirty();
    return 1U;
}
