#include "Core/live_parameter_audio_runtime.h"

#include "Core/live_parameter_audio_queue.h"
#include "Core/live_parameter_event.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_mod_matrix.h"
#include "Param/param_registry.h"
#include "memory_layout.h"

typedef struct
{
    uint8_t valid;
    uint8_t scope;
    uint8_t track;
    uint8_t slot;
    uint16_t parameter_id;
    uint16_t reserved;
    float target;
    uint64_t last_sample_time;
} live_parameter_audio_runtime_slot_t;

SEQ_STATE_D2 static live_parameter_audio_runtime_slot_t
    g_live_parameter_audio_runtime_slots[LIVE_PARAMETER_AUDIO_RUNTIME_SLOT_CAPACITY];
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

static uint8_t live_parameter_audio_runtime_slot_matches(
    const live_parameter_audio_runtime_slot_t *slot,
    const live_parameter_audio_event_t *event)
{
    return (uint8_t)((slot->valid != 0U)
                     && (slot->parameter_id == event->parameter_id)
                     && (slot->scope == event->scope)
                     && (slot->track == event->track)
                     && (slot->slot == event->slot));
}

static live_parameter_audio_runtime_slot_t *
live_parameter_audio_runtime_find_slot(const live_parameter_audio_event_t *event)
{
    live_parameter_audio_runtime_slot_t *free_slot = 0;
    live_parameter_audio_runtime_slot_t *reusable_slot = 0;
    uint64_t reusable_age = UINT64_MAX;
    for (uint16_t i = 0U; i < LIVE_PARAMETER_AUDIO_RUNTIME_SLOT_CAPACITY; ++i)
    {
        live_parameter_audio_runtime_slot_t *const slot =
            &g_live_parameter_audio_runtime_slots[i];
        if (live_parameter_audio_runtime_slot_matches(slot, event) != 0U)
            return slot;
        if ((free_slot == 0) && (slot->valid == 0U))
            free_slot = slot;
        if ((slot->valid != 0U) && (slot->last_sample_time < reusable_age))
        {
            reusable_slot = slot;
            reusable_age = slot->last_sample_time;
        }
    }
    return (free_slot != 0) ? free_slot : reusable_slot;
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
            uint8_t applied;
            if ((event->matrix_operation == LIVE_PARAMETER_MATRIX_OPERATION_LFO_TEMP_CLEAR)
                    || ((event->matrix_operation == LIVE_PARAMETER_MATRIX_OPERATION_OVERRIDE_CLEAR)
                        && (param_registry_is_lfo_param(event->parameter_id) != 0U)))
            {
                applied = param_registry_clear_track_value_runtime_temp_audio(
                    event->parameter_id, event->track);
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
            audio_mod_matrix_rebuild_track(event->track);
        }
        return applied;
    }
    if (event->scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
    {
        if (event->parameter_id == PARAM_MASTER_GAIN)
            return audio_note_engine_adapter_set_master(value);
        return param_registry_apply_global_value_rt_fast(event->parameter_id, value);
    }
    return 0U;
}

static uint8_t live_parameter_audio_runtime_apply_event(
    const live_parameter_audio_event_t *event,
    uint64_t now)
{
    if ((event == 0) || (event->parameter_id >= PARAM_COUNT))
        return 0U;

    live_parameter_audio_runtime_slot_t *const slot =
        live_parameter_audio_runtime_find_slot(event);
    if (slot == 0)
        return 0U;

    const uint8_t new_slot = (slot->valid == 0U) ? 1U : 0U;
    if ((new_slot != 0U)
            || (live_parameter_audio_runtime_slot_matches(slot, event) == 0U))
    {
        slot->valid = 1U;
        slot->scope = event->scope;
        slot->track = event->track;
        slot->slot = event->slot;
        slot->parameter_id = event->parameter_id;
        slot->last_sample_time = now;
    }

    const float target = live_parameter_audio_runtime_clamp(
        event->parameter_id,
        ((event->flags & LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS) != 0U)
            ? live_parameter_event_decode_float(event->value)
            : (float)event->value);
    slot->target = target;
    slot->last_sample_time = now;

    if (live_parameter_audio_runtime_apply_target(event, target) == 0U)
        return 0U;
    return 1U;
}

void live_parameter_audio_runtime_init(void)
{
    for (uint16_t i = 0U; i < LIVE_PARAMETER_AUDIO_RUNTIME_SLOT_CAPACITY; ++i)
    {
        g_live_parameter_audio_runtime_slots[i] =
            (live_parameter_audio_runtime_slot_t){ 0 };
    }
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
    for (uint16_t i = 0U; i < LIVE_PARAMETER_AUDIO_RUNTIME_SLOT_CAPACITY; ++i)
    {
        live_parameter_audio_event_t event;
        if (live_parameter_audio_queue_pop_due(&event) == 0U)
            break;
        if (live_parameter_audio_runtime_apply_event(&event, now) != 0U)
            ++applied;
    }
    return applied;
}

void live_parameter_audio_runtime_process(uint64_t block_start,
                                          uint16_t frames)
{
    (void)block_start;
    (void)frames;
}
