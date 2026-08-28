#include "Core/live_parameter_audio_runtime.h"

#include "Audio/audio_global_runtime.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Core/live_parameter_event.h"
#include "Param/param_registry.h"
#include "Storage/memory_layout.h"

#include <string.h>

SEQ_STATE_D2 static float g_live_parameter_audio_poly_voices[SEQ_LANE_CAPACITY];
SEQ_STATE_D2 static float g_live_parameter_audio_poly_spread[SEQ_LANE_CAPACITY];
AUDIO_WARM static float
    g_live_parameter_audio_track_value[SEQ_LANE_CAPACITY][PARAM_COUNT];
AUDIO_WARM static uint32_t
    g_live_parameter_audio_track_valid[SEQ_LANE_CAPACITY][(PARAM_COUNT + 31U) / 32U];

static void live_parameter_audio_runtime_store(uint8_t entity,
                                               param_id_t parameter,
                                               float value)
{
    g_live_parameter_audio_track_value[entity][parameter] = value;
    g_live_parameter_audio_track_valid[entity][parameter >> 5U] |=
        (uint32_t)1U << (parameter & 31U);
}

static float live_parameter_audio_runtime_clamp(param_id_t parameter,
                                                float value)
{
    if (parameter >= PARAM_COUNT) return value;
    if (value < param_registry[parameter].min)
        return param_registry[parameter].min;
    if (value > param_registry[parameter].max)
        return param_registry[parameter].max;
    return value;
}

static uint8_t live_parameter_audio_runtime_changes_matrix_context(param_id_t id)
{
    switch (id)
    {
        case PARAM_AUDIO_FX_MODEL: case PARAM_AUDIO_FX_B_MODEL:
        case PARAM_DRUM_MD_MODEL:
        case PARAM_PRISM_OSC1_MODEL: case PARAM_PRISM_OSC2_MODEL:
        case PARAM_STACK_OSC1_MODEL: case PARAM_STACK_OSC2_MODEL:
        case PARAM_STACK_OSC3_MODEL:
        case PARAM_LFO1_TRIG: case PARAM_LFO2_TRIG: case PARAM_LFO3_TRIG:
            return 1U;
        default: return 0U;
    }
}

void live_parameter_audio_runtime_init(void)
{
    memset(g_live_parameter_audio_track_value, 0,
           sizeof(g_live_parameter_audio_track_value));
    memset(g_live_parameter_audio_track_valid, 0,
           sizeof(g_live_parameter_audio_track_valid));
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        g_live_parameter_audio_poly_voices[track] =
            param_registry[PARAM_CFG_POLY_VOICES].default_value;
        g_live_parameter_audio_poly_spread[track] =
            param_registry[PARAM_CFG_POLY_SPREAD].default_value;
    }
}

void live_parameter_audio_runtime_initialize_program(uint8_t entity)
{
    if (entity >= SEQ_LANE_CAPACITY) return;
    for (param_id_t id = 0U; id < PARAM_COUNT; ++id)
    {
        const uint32_t mask = (uint32_t)1U << (id & 31U);
        if ((g_live_parameter_audio_track_valid[entity][id >> 5U] & mask) == 0U)
            continue;
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE) continue;
        (void)param_registry_apply_track_value_audio(
            id, entity, g_live_parameter_audio_track_value[entity][id]);
    }
}

uint8_t live_parameter_audio_runtime_apply_param(uint8_t entity,
                                                 uint16_t parameter_id,
                                                 uint32_t value_bits,
                                                 uint8_t scope)
{
    if (parameter_id >= PARAM_COUNT) return 0U;
    const float decoded = live_parameter_event_decode_float((int32_t)value_bits);
    if ((scope >= LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE)
            && (scope <= LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_LAST))
        return audio_mod_matrix_apply_param(entity,
            (uint8_t)(scope - LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE),
            (param_id_t)parameter_id, decoded);
    if (scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
    {
        if (parameter_id == PARAM_MASTER_GAIN)
            return audio_note_engine_adapter_set_master(decoded);
        return audio_global_runtime_apply(parameter_id, decoded);
    }
    if ((scope != LIVE_PARAMETER_EVENT_SCOPE_TRACK)
            || (entity >= SEQ_LANE_CAPACITY))
        return 0U;

    const float value = live_parameter_audio_runtime_clamp(
        (param_id_t)parameter_id, decoded);
    live_parameter_audio_runtime_store(entity, (param_id_t)parameter_id, value);
    if ((parameter_id >= PARAM_MOD_MULTI_1_A)
            && (parameter_id <= PARAM_MOD_SLEW_2_AMOUNT))
        return audio_mod_matrix_apply_param(entity, UINT8_MAX,
                                            (param_id_t)parameter_id, value);
    if ((parameter_id == PARAM_CFG_POLY_VOICES)
            || (parameter_id == PARAM_CFG_POLY_SPREAD))
    {
        if (parameter_id == PARAM_CFG_POLY_VOICES)
            g_live_parameter_audio_poly_voices[entity] = value;
        else
            g_live_parameter_audio_poly_spread[entity] = value;
        const uint8_t applied = audio_note_engine_adapter_apply_polyphony(
            entity, (uint8_t)g_live_parameter_audio_poly_voices[entity],
            g_live_parameter_audio_poly_spread[entity]);
        if (applied != 0U)
            audio_mod_matrix_base_update(entity, parameter_id, value);
        return applied;
    }

    const uint8_t applied = param_registry_apply_track_value_audio(
        parameter_id, entity, value);
    if (applied != 0U)
    {
        audio_mod_matrix_base_update(entity, parameter_id, value);
        if (live_parameter_audio_runtime_changes_matrix_context(
                (param_id_t)parameter_id) != 0U)
        {
            audio_mod_matrix_rebuild_track(entity);
        }
    }
    return applied;
}
