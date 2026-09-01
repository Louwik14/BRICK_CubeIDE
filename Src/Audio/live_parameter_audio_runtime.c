#include "Audio/live_parameter_audio_runtime.h"

#include <string.h>
#include <math.h>

#include "Audio/audio_global_runtime.h"
#include "Audio/audio_mod_matrix.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_fx_runtime.h"
#include "IPC/live_parameter_event.h"
#include "IPC/control_audio_command.h"
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
        case PARAM_DRUM_MD_MODEL:
        case PARAM_PRISM_OSC1_MODEL: case PARAM_PRISM_OSC2_MODEL:
        case PARAM_STACK_OSC1_MODEL: case PARAM_STACK_OSC2_MODEL:
        case PARAM_STACK_OSC3_MODEL:
        case PARAM_LFO1_TRIG: case PARAM_LFO2_TRIG: case PARAM_LFO3_TRIG:
            return 1U;
        default: return 0U;
    }
}

static uint8_t live_parameter_audio_runtime_exact_u8(float value,
                                                     uint8_t *out_value)
{
    if ((out_value == NULL) || !isfinite(value) || (value < 0.0f)
            || (value > 255.0f) || (value != floorf(value))) return 0U;
    *out_value = (uint8_t)value;
    return 1U;
}

static uint8_t live_parameter_audio_runtime_exact_u16(float value,
                                                      uint16_t *out_value)
{
    if ((out_value == NULL) || !isfinite(value) || (value < 0.0f)
            || (value > 65535.0f) || (value != floorf(value))) return 0U;
    *out_value = (uint16_t)value;
    return 1U;
}

void live_parameter_audio_runtime_init(void)
{
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        g_live_parameter_audio_poly_voices[track] = 1.0f;
        g_live_parameter_audio_poly_spread[track] =
            param_spec[PARAM_CFG_POLY_SPREAD].default_value;
    }
}

uint8_t live_parameter_audio_runtime_apply_param(uint8_t entity,
                                                 uint16_t parameter_id,
                                                 uint32_t value_bits,
                                                 uint8_t scope)
{
    const float decoded = live_parameter_event_decode_float((int32_t)value_bits);
    if (parameter_id == CONTROL_AUDIO_PARAM_CLEAR_RUNTIME_TEMP)
    {
        uint16_t endpoint = 0U;
        if ((entity >= BRICK_ENTITY_CAPACITY)
                || (scope != LIVE_PARAMETER_EVENT_SCOPE_TRACK)
                || (live_parameter_audio_runtime_exact_u16(
                    decoded, &endpoint) == 0U)
                || (endpoint >= PARAM_COUNT)) return 0U;
        return param_audio_clear_track_temp((param_id_t)endpoint, entity);
    }
    if (parameter_id == CONTROL_AUDIO_CONFIG_POLY_VOICES)
    {
        uint8_t voices = 0U;
        if ((entity >= BRICK_ENTITY_CAPACITY)
                || (scope != LIVE_PARAMETER_EVENT_SCOPE_TRACK)
                || (live_parameter_audio_runtime_exact_u8(
                    decoded, &voices) == 0U)) return 0U;
        g_live_parameter_audio_poly_voices[entity] = (float)voices;
        return audio_note_engine_adapter_apply_polyphony(entity,
            voices,
            g_live_parameter_audio_poly_spread[entity]);
    }
    if ((parameter_id >= CONTROL_AUDIO_MOD_ROUTE_SOURCE)
            && (parameter_id <= CONTROL_AUDIO_MOD_SLEW_AMOUNT))
    {
        if ((entity >= BRICK_ENTITY_CAPACITY)
                || (scope < LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE)) return 0U;
        const uint8_t index = (uint8_t)(scope - LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE);
        uint8_t value_u8 = 0U;
        uint16_t value_u16 = 0U;
        switch (parameter_id)
        {
            case CONTROL_AUDIO_MOD_ROUTE_SOURCE:
                return live_parameter_audio_runtime_exact_u8(decoded,&value_u8)
                    && audio_mod_matrix_set_route_source(entity,index,value_u8);
            case CONTROL_AUDIO_MOD_ROUTE_DESTINATION:
                return live_parameter_audio_runtime_exact_u16(decoded,&value_u16)
                    && audio_mod_matrix_set_route_destination(entity,index,value_u16);
            case CONTROL_AUDIO_MOD_ROUTE_DEPTH: return audio_mod_matrix_set_route_depth(entity,index,decoded);
            case CONTROL_AUDIO_MOD_ROUTE_ENABLED:
                return live_parameter_audio_runtime_exact_u8(decoded,&value_u8)
                    && value_u8<=1U
                    && audio_mod_matrix_set_route_enabled(entity,index,value_u8);
            case CONTROL_AUDIO_MOD_MULTI_SOURCE:
                return live_parameter_audio_runtime_exact_u8(decoded,&value_u8)
                    && audio_mod_matrix_set_multi_source(entity,
                        (uint8_t)(index>>1U),(uint8_t)(index&1U),value_u8);
            case CONTROL_AUDIO_MOD_SLEW_SOURCE:
                return live_parameter_audio_runtime_exact_u8(decoded,&value_u8)
                    && audio_mod_matrix_set_slew_source(entity,index,value_u8);
            case CONTROL_AUDIO_MOD_SLEW_AMOUNT: return audio_mod_matrix_set_slew_amount(entity,index,decoded);
            default: return 0U;
        }
    }
    if ((parameter_id >= CONTROL_AUDIO_FX_FILTER_POSITION)
            && (parameter_id <= CONTROL_AUDIO_FX_SPATIAL_MODE))
    {
        if ((entity >= BRICK_ENTITY_CAPACITY)
                || (scope < LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE)) return 0U;
        const uint8_t index=(uint8_t)(scope-LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE);
        uint8_t typed_value=0U;
        if(live_parameter_audio_runtime_exact_u8(decoded,&typed_value)==0U)return 0U;
        if(parameter_id==CONTROL_AUDIO_FX_FILTER_POSITION)
            return typed_value<AUDIO_FX_FILTER_POS_COUNT
                &&audio_fx_runtime_set_filter_pos(entity,(audio_fx_filter_pos_t)typed_value);
        if(parameter_id==CONTROL_AUDIO_FX_ORDER)
            return typed_value<AUDIO_FX_ORDER_COUNT
                &&audio_fx_runtime_set_order(entity,(audio_fx_order_t)typed_value);
        return typed_value<4U&&audio_fx_runtime_set_spatial_mode(
            entity,(audio_fx_slot_t)index,typed_value);
    }
    if (parameter_id >= PARAM_COUNT) return 0U;
    if ((scope >= LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE)
            && (scope <= LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_LAST))
        return 0U;
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
    if (parameter_id == PARAM_CFG_POLY_SPREAD)
    {
        g_live_parameter_audio_poly_spread[entity] = value;
        const uint8_t applied = audio_note_engine_adapter_apply_polyphony(
            entity, (uint8_t)g_live_parameter_audio_poly_voices[entity],
            g_live_parameter_audio_poly_spread[entity]);
        if (applied != 0U)
            audio_mod_matrix_base_update(entity, parameter_id, value);
        return applied;
    }

    uint8_t applied = 0U;
    if (scope == LIVE_PARAMETER_AUDIO_SCOPE_RUNTIME_TEMP)
        applied = param_audio_apply_track_temp(
            (param_id_t)parameter_id, entity, value);
    else
    {
        /* A Matrix destination command may immediately precede its durable
         * base projection in the same FIFO bulk.  Materialize the route
         * before the normal base update is applied. */
        audio_mod_matrix_finalize_dirty();
        applied = param_audio_apply_track(&(const param_audio_value_t){
            .id = (param_id_t)parameter_id, .value = value }, entity);
    }
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
