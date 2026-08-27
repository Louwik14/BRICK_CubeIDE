#include "Core/live_parameter_audio_runtime.h"

#include "Core/live_parameter_audio_queue.h"
#include "Core/live_parameter_event.h"
#include "Audio/audio_global_runtime.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_mod_matrix.h"
#include "Param/param_registry.h"
#include "Param/param_value_policy.h"
#include "memory_layout.h"

SEQ_STATE_D2 static float g_live_parameter_audio_poly_voices[SEQ_LANE_CAPACITY];
SEQ_STATE_D2 static float g_live_parameter_audio_poly_spread[SEQ_LANE_CAPACITY];

static float live_parameter_audio_runtime_clamp(param_id_t parameter, float value)
{
    if (parameter >= PARAM_COUNT)
        return value;
    if (value < param_registry[parameter].min)
        return param_registry[parameter].min;
    if (value > param_registry[parameter].max)
        return param_registry[parameter].max;
    return value;
}

static uint8_t live_parameter_audio_runtime_apply_target(
    const live_parameter_audio_event_t *event,
    float value)
{
    if ((event == 0) || (event->parameter_id >= PARAM_COUNT))
        return 0U;

    if (event->scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
    {
        if ((event->flags & LIVE_PARAMETER_EVENT_FLAG_RUNTIME_TEMP) != 0U)
        {
            if (event->matrix_operation
                    == LIVE_PARAMETER_MATRIX_OPERATION_BASE_UPDATE)
            {
                audio_mod_matrix_base_update(
                    event->track, event->parameter_id, value);
                return 1U;
            }
            uint8_t applied;
            if (event->matrix_operation == LIVE_PARAMETER_MATRIX_OPERATION_LFO_TEMP_CLEAR)
            {
                applied = param_registry_clear_track_value_runtime_temp_audio(
                    event->parameter_id, event->track);
            }
            else if (event->matrix_operation == LIVE_PARAMETER_MATRIX_OPERATION_OVERRIDE_CLEAR)
            {
                applied = param_registry_clear_track_value_runtime_temp_audio(
                    event->parameter_id, event->track);
                if (applied == 0U)
                {
                    applied = param_registry_apply_track_value_runtime_temp_audio(
                        event->parameter_id, event->track, value);
                }
            }
            else
            {
                applied = param_registry_apply_track_value_runtime_temp_audio(
                    event->parameter_id, event->track, value);
            }
            if (applied == 0U)
                return 0U;
            if (event->matrix_operation == LIVE_PARAMETER_MATRIX_OPERATION_OVERRIDE_SET)
            {
                audio_mod_matrix_set_base_override(
                    event->track, event->parameter_id, value);
            }
            else if (event->matrix_operation
                     == LIVE_PARAMETER_MATRIX_OPERATION_OVERRIDE_CLEAR)
            {
                audio_mod_matrix_clear_base_override(
                    event->track, event->parameter_id, value);
            }
            return 1U;
        }
        if ((event->parameter_id == PARAM_CFG_POLY_VOICES)
                || (event->parameter_id == PARAM_CFG_POLY_SPREAD))
        {
            if (event->track >= SEQ_LANE_CAPACITY)
                return 0U;
            if (event->parameter_id == PARAM_CFG_POLY_VOICES)
                g_live_parameter_audio_poly_voices[event->track] = value;
            else
                g_live_parameter_audio_poly_spread[event->track] = value;
            const uint8_t applied = audio_note_engine_adapter_apply_polyphony(
                event->track,
                (uint8_t)g_live_parameter_audio_poly_voices[event->track],
                g_live_parameter_audio_poly_spread[event->track]);
            if (applied != 0U)
            {
                audio_mod_matrix_base_update(
                    event->track, event->parameter_id, value);
            }
            return applied;
        }
        const uint8_t applied = param_registry_apply_track_value_audio(
            event->parameter_id, event->track, value);
        if (applied != 0U)
        {
            audio_mod_matrix_base_update(
                event->track, event->parameter_id, value);
        }
        return applied;
    }
    if (event->scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
    {
        if (event->parameter_id == PARAM_MASTER_GAIN)
            return audio_note_engine_adapter_set_master(value);
        return audio_global_runtime_apply(event->parameter_id, value);
    }
    return 0U;
}

static uint8_t live_parameter_audio_runtime_apply_event(
    const live_parameter_audio_event_t *event)
{
    if ((event == 0) || (event->parameter_id >= PARAM_COUNT))
        return 0U;

    float target = ((event->flags & LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS) != 0U)
        ? live_parameter_event_decode_float(event->value)
        : (float)event->value;
    if (event->scope != LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
        target = live_parameter_audio_runtime_clamp(event->parameter_id, target);
    if (live_parameter_audio_runtime_apply_target(event, target) == 0U)
        return 0U;
    return 1U;
}

void live_parameter_audio_runtime_init(void)
{
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        g_live_parameter_audio_poly_voices[track] =
            param_registry[PARAM_CFG_POLY_VOICES].default_value;
        g_live_parameter_audio_poly_spread[track] =
            param_registry[PARAM_CFG_POLY_SPREAD].default_value;
    }
}

uint16_t live_parameter_audio_runtime_apply_due(uint64_t now)
{
    uint16_t applied = 0U;
    for (;;)
    {
        live_parameter_audio_event_t event;
        if ((live_parameter_audio_queue_audio_peek(&event) == false)
                || (event.effective_sample_time > now))
            break;
        if (live_parameter_audio_runtime_apply_event(&event) != 0U)
            ++applied;
        (void)live_parameter_audio_queue_audio_pop();
    }
    for (;;)
    {
        live_parameter_audio_dated_event_t dated;
        if (live_parameter_audio_queue_audio_peek_dated(&dated) == false)
            break;
        if ((int16_t)(dated.due_sample_low - (uint16_t)now) > 0)
            break;
        const live_parameter_audio_event_t event = {
            .effective_sample_time = now,
            .parameter_id = dated.parameter_id,
            .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = dated.track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                                | LIVE_PARAMETER_EVENT_FLAG_RUNTIME_TEMP
                                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
            .value = live_parameter_event_encode_float(
                param_value_policy_decode_u16(
                    &param_registry[dated.parameter_id], dated.value16)),
            .matrix_operation = dated.matrix_operation
        };
        if (live_parameter_audio_runtime_apply_event(&event) != 0U)
            ++applied;
        (void)live_parameter_audio_queue_audio_pop_dated();
    }
    return applied;
}
