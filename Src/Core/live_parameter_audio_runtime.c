#include "Core/live_parameter_audio_runtime.h"

#include "Core/live_parameter_audio_queue.h"
#include "Core/live_parameter_event.h"
#include "Param/param_registry.h"
#include "Param/param_registry_runtime_state.h"
#include "param_store.h"
#include "memory_layout.h"

typedef struct
{
    uint8_t valid;
    uint8_t scope;
    uint8_t track;
    uint8_t slot;
    uint16_t parameter_id;
    uint16_t reserved;
    float current;
    float target;
    float increment;
    uint32_t remaining_samples;
    uint64_t last_sample_time;
} live_parameter_audio_runtime_slot_t;

SEQ_STATE_D2 static live_parameter_audio_runtime_slot_t
    g_live_parameter_audio_runtime_slots[LIVE_PARAMETER_AUDIO_RUNTIME_SLOT_CAPACITY];
SEQ_STATE_D2 static live_parameter_audio_runtime_diag_t g_live_parameter_audio_runtime_diag;

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
        if ((slot->valid != 0U)
                && (slot->remaining_samples == 0U)
                && (slot->last_sample_time < reusable_age))
        {
            reusable_slot = slot;
            reusable_age = slot->last_sample_time;
        }
    }
    return (free_slot != 0) ? free_slot : reusable_slot;
}

static void live_parameter_audio_runtime_advance_to(
    live_parameter_audio_runtime_slot_t *slot,
    uint64_t sample_time)
{
    if ((slot == 0) || (slot->valid == 0U) || (sample_time <= slot->last_sample_time))
        return;

    uint64_t elapsed = sample_time - slot->last_sample_time;
    if (slot->remaining_samples != 0U)
    {
        if (elapsed > (uint64_t)slot->remaining_samples)
            elapsed = (uint64_t)slot->remaining_samples;

        slot->current += slot->increment * (float)elapsed;
        slot->remaining_samples -= (uint32_t)elapsed;
        if (slot->remaining_samples == 0U)
        {
            slot->current = slot->target;
            slot->increment = 0.0f;
        }
    }

    slot->last_sample_time = sample_time;
}

static uint8_t live_parameter_audio_runtime_read_current(
    const live_parameter_audio_event_t *event,
    float *out_value)
{
    if ((event == 0) || (out_value == 0) || (event->parameter_id >= PARAM_COUNT))
        return 0U;

    if (event->scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
    {
        return param_registry_runtime_get_or_default(param_registry,
                                                     event->parameter_id,
                                                     event->track,
                                                     out_value);
    }
    if (event->scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
    {
        *out_value = param_get(event->parameter_id);
        return 1U;
    }
    return 0U;
}

static uint8_t live_parameter_audio_runtime_apply_target(
    const live_parameter_audio_event_t *event,
    float value)
{
    if ((event == 0) || (event->parameter_id >= PARAM_COUNT))
        return 0U;

    if (event->scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
    {
        return param_registry_apply_track_value_audio(event->parameter_id,
                                                       event->track,
                                                       value);
    }
    if (event->scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
    {
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
    {
        g_live_parameter_audio_runtime_diag.slot_drop_count++;
        return 0U;
    }

    const uint8_t new_slot = (slot->valid == 0U) ? 1U : 0U;
    if ((new_slot != 0U)
            || (live_parameter_audio_runtime_slot_matches(slot, event) == 0U))
    {
        float current = 0.0f;
        if (live_parameter_audio_runtime_read_current(event, &current) == 0U)
        {
            g_live_parameter_audio_runtime_diag.rejected_count++;
            return 0U;
        }

        slot->valid = 1U;
        slot->scope = event->scope;
        slot->track = event->track;
        slot->slot = event->slot;
        slot->parameter_id = event->parameter_id;
        slot->current = live_parameter_audio_runtime_clamp(event->parameter_id, current);
        slot->target = slot->current;
        slot->increment = 0.0f;
        slot->remaining_samples = 0U;
        slot->last_sample_time = now;
        if (new_slot != 0U)
        {
            g_live_parameter_audio_runtime_diag.active_slots++;
            if (g_live_parameter_audio_runtime_diag.active_slots
                    > g_live_parameter_audio_runtime_diag.high_water)
            {
                g_live_parameter_audio_runtime_diag.high_water =
                    g_live_parameter_audio_runtime_diag.active_slots;
            }
        }
    }
    else
    {
        live_parameter_audio_runtime_advance_to(slot, now);
        g_live_parameter_audio_runtime_diag.retarget_count++;
    }

    const float target = live_parameter_audio_runtime_clamp(
        event->parameter_id,
        ((event->flags & LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS) != 0U)
            ? live_parameter_event_decode_float(event->value)
            : (float)event->value);
    slot->target = target;
    if (slot->current == target)
    {
        slot->current = target;
        slot->increment = 0.0f;
        slot->remaining_samples = 0U;
    }
    else
    {
        slot->increment = (target - slot->current)
                        / (float)LIVE_PARAMETER_AUDIO_RUNTIME_RAMP_SAMPLES;
        slot->remaining_samples = LIVE_PARAMETER_AUDIO_RUNTIME_RAMP_SAMPLES;
    }
    slot->last_sample_time = now;

    if (live_parameter_audio_runtime_apply_target(event, target) == 0U)
    {
        g_live_parameter_audio_runtime_diag.rejected_count++;
        return 0U;
    }

    g_live_parameter_audio_runtime_diag.applied_count++;
    return 1U;
}

void live_parameter_audio_runtime_init(void)
{
    for (uint16_t i = 0U; i < LIVE_PARAMETER_AUDIO_RUNTIME_SLOT_CAPACITY; ++i)
    {
        g_live_parameter_audio_runtime_slots[i] =
            (live_parameter_audio_runtime_slot_t){ 0 };
    }
    g_live_parameter_audio_runtime_diag =
        (live_parameter_audio_runtime_diag_t){ 0 };
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
    if (frames == 0U)
        return;

    uint64_t block_end = block_start + (uint64_t)frames;
    if (block_end < block_start)
        block_end = UINT64_MAX;

    for (uint16_t i = 0U; i < LIVE_PARAMETER_AUDIO_RUNTIME_SLOT_CAPACITY; ++i)
    {
        live_parameter_audio_runtime_advance_to(
            &g_live_parameter_audio_runtime_slots[i], block_end);
    }
}

void live_parameter_audio_runtime_get_diag(
    live_parameter_audio_runtime_diag_t *out_diag)
{
    if (out_diag == 0)
        return;
    *out_diag = g_live_parameter_audio_runtime_diag;
}
