#include "Audio/live_parameter_audio_runtime.h"

#include <string.h>

#include "Audio/audio_global_runtime.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/audio_note_engine_adapter.h"
#include "IPC/live_parameter_event.h"
#include "Param/param_audio.h"
#include "Param/param_spec.h"
#include "Track/entity_types.h"
#include "Platform/memory_layout.h"

SEQ_STATE_D2 static float g_live_parameter_audio_poly_voices[BRICK_ENTITY_CAPACITY];
SEQ_STATE_D2 static float g_live_parameter_audio_poly_spread[BRICK_ENTITY_CAPACITY];
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
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        g_live_parameter_audio_poly_voices[track] =
            param_spec[PARAM_CFG_POLY_VOICES].default_value;
        g_live_parameter_audio_poly_spread[track] =
            param_spec[PARAM_CFG_POLY_SPREAD].default_value;
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
    if (((scope != LIVE_PARAMETER_EVENT_SCOPE_TRACK)
            && (scope != LIVE_PARAMETER_AUDIO_SCOPE_RUNTIME_TEMP))
            || (entity >= BRICK_ENTITY_CAPACITY))
        return 0U;

    if (param_spec_value_is_valid((param_id_t)parameter_id, decoded) == 0U)
        return 0U;
    const float value = decoded;
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

    const uint8_t applied = (scope == LIVE_PARAMETER_AUDIO_SCOPE_RUNTIME_TEMP)
        ? param_audio_apply_track_temp((param_id_t)parameter_id, entity, value)
        : param_audio_apply_track(&(const param_audio_value_t){
            .id = (param_id_t)parameter_id, .value = value }, entity);
    if ((applied != 0U) && (scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK))
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
