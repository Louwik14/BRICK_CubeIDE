#include "IPC/live_parameter_audio_publication.h"

#include <stddef.h>

#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "IPC/live_clock_control.h"
#include "App/live_parameter_event_control.h"
#include "Storage/project_control.h"
#include "Param/param_value_policy.h"
#include "Track/track_runtime.h"
#include "Seq/seq_runtime_exec.h"

static uint32_t g_live_parameter_audio_publish_failure_count;

static bool live_parameter_audio_publish_failed(void)
{
    ++g_live_parameter_audio_publish_failure_count;
    return false;
}

static uint8_t live_parameter_audio_project_sampler_value(
    uint16_t parameter_id, uint8_t track, float logical_value,
    float *out_runtime_value)
{
    if (parameter_id != (uint16_t)PARAM_SAMPLER_SAMPLE)
    {
        if (out_runtime_value != NULL) *out_runtime_value = logical_value;
        return 1U;
    }
    return project_control_resolve_audio_sampler_value(
        track, logical_value, out_runtime_value);
}

static uint8_t live_parameter_audio_convert_capture(
    uint32_t capture_tick, uint64_t *out_effective_sample_time)
{
    if (live_clock_tim5_to_guarded_sample_time(
            capture_tick, out_effective_sample_time) == 0U)
        return 0U;
    const uint64_t now = seq_runtime_exec_get_sample_timeline();
    if (*out_effective_sample_time < now)
        *out_effective_sample_time = now;
    return 1U;
}

static uint8_t live_parameter_audio_make_command(
    uint64_t due_sample, uint16_t parameter_id, uint8_t scope,
    uint8_t track, uint8_t slot, uint16_t flags, int32_t raw_value,
    control_audio_command_t *out_command)
{
    if ((out_command == NULL) || (parameter_id >= PARAM_COUNT))
        return 0U;
    float value = ((flags & LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS) != 0U)
        ? live_parameter_event_decode_float(raw_value) : (float)raw_value;
    if ((scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
            && (live_parameter_audio_project_sampler_value(
                parameter_id, track, value, &value) == 0U))
        return 0U;
    uint8_t command_scope = scope;
    if (scope == LIVE_PARAMETER_EVENT_SCOPE_SLOT)
    {
        if (slot >= 8U) return 0U;
        command_scope = (uint8_t)(LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE + slot);
    }
    *out_command = (control_audio_command_t){
        .effective_sample_time = due_sample,
        .value = (uint32_t)live_parameter_event_encode_float(value),
        .id = parameter_id,
        .entity = (track < BRICK_ENTITY_CAPACITY) ? track : 0U,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
            CONTROL_AUDIO_COMMAND_PARAM, command_scope & 0x1FU)
    };
    return 1U;
}

bool live_parameter_audio_publication_submit_tone_state(
    uint8_t track, const float values[SEQ_PARAM_TONE_SLOT_COUNT])
{
    if ((track >= SEQ_LANE_CAPACITY) || (values == NULL))
        return live_parameter_audio_publish_failed();
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = 0U
    };
    track_runtime_descriptor_t descriptor;
    if (track_runtime_get_descriptor(track, &descriptor) == 0U)
        return live_parameter_audio_publish_failed();
    for (uint8_t slot = 0U; slot < SEQ_PARAM_TONE_SLOT_COUNT; ++slot)
    {
        param_id_t id = PARAM_COUNT;
        if ((track_runtime_tone_slot_to_param(descriptor.type, slot, &id) == 0U)
                || (param_registry_track_value_is_audio_command(id, track) == 0U))
            continue;
        const float normalized = (values[slot] < 0.0f) ? 0.0f
            : ((values[slot] > 1.0f) ? 1.0f : values[slot]);
        const float value = param_registry[id].min
            + normalized * (param_registry[id].max - param_registry[id].min);
        bulk.item[bulk.count++] = (live_parameter_audio_bulk_item_t){
            .parameter_id = id,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
            .value = live_parameter_event_encode_float(value)
        };
    }
    if (bulk.count == 0U) return true;
    return live_parameter_audio_publication_submit_bulk(&bulk);
}

void live_parameter_audio_publication_init(void)
{
    g_live_parameter_audio_publish_failure_count = 0U;
}

uint16_t live_parameter_audio_publication_drain(void)
{
    uint16_t drained = 0U;
    for (uint16_t i = 0U; i < LIVE_PARAMETER_CONTROL_EVENT_DRAIN_BUDGET; ++i)
    {
        live_parameter_event_t event;
        uint64_t due_sample;
        control_audio_command_t command;
        if ((live_parameter_event_peek(&event) == 0U)
                || (live_parameter_audio_convert_capture(
                    event.capture_tick, &due_sample) == 0U)
                || (live_parameter_audio_make_command(
                    due_sample, event.parameter_id, event.scope, event.track,
                    event.slot, event.flags, event.value, &command) == 0U)
                || (control_audio_publish_param(
                    command.entity, command.id, command.value,
                    CONTROL_AUDIO_COMMAND_KIND(&command),
                    command.effective_sample_time) == 0U))
            break;
        live_parameter_event_consume();
        ++drained;
    }
    return drained;
}

bool live_parameter_audio_publication_submit_bulk(
    const live_parameter_audio_bulk_t *bulk)
{
    if ((bulk == NULL) || (bulk->count == 0U)
            || (bulk->count > LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS))
        return live_parameter_audio_publish_failed();
    uint64_t due_sample;
    if (live_parameter_audio_convert_capture(
            bulk->capture_tick, &due_sample) == 0U)
        return live_parameter_audio_publish_failed();

    control_audio_command_t commands[LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS];
    for (uint8_t i = 0U; i < bulk->count; ++i)
    {
        const live_parameter_audio_bulk_item_t *const item = &bulk->item[i];
        for (uint8_t previous = 0U; previous < i; ++previous)
        {
            const live_parameter_audio_bulk_item_t *const prior =
                &bulk->item[previous];
            if ((prior->parameter_id == item->parameter_id)
                    && (prior->scope == item->scope)
                    && (prior->track == item->track)
                    && (prior->slot == item->slot))
                return live_parameter_audio_publish_failed();
        }
        if (live_parameter_audio_make_command(
                due_sample, item->parameter_id, item->scope, item->track,
                item->slot, item->flags, item->value, &commands[i]) == 0U)
            return live_parameter_audio_publish_failed();
    }
    return control_audio_publish_batch(commands, bulk->count) != 0U
        ? true : live_parameter_audio_publish_failed();
}

bool live_parameter_audio_publication_submit_poly_pair(
    uint32_t capture_tick, uint8_t track, float voices, float spread)
{
    if (track >= SEQ_LANE_CAPACITY)
        return live_parameter_audio_publish_failed();
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = capture_tick,
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = 2U
    };
    bulk.item[0] = (live_parameter_audio_bulk_item_t){
        .parameter_id = PARAM_CFG_POLY_VOICES,
        .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
        .track = track,
        .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .flags = LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
        .value = live_parameter_event_encode_float(voices)
    };
    bulk.item[1] = (live_parameter_audio_bulk_item_t){
        .parameter_id = PARAM_CFG_POLY_SPREAD,
        .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
        .track = track,
        .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .flags = LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
        .value = live_parameter_event_encode_float(spread)
    };
    return live_parameter_audio_publication_submit_bulk(&bulk);
}

bool live_parameter_audio_publication_submit_dated(
    uint64_t effective_sample_time, uint16_t parameter_id, uint8_t track,
    uint16_t value16)
{
    if ((parameter_id >= PARAM_COUNT) || (track >= SEQ_LANE_CAPACITY))
        return live_parameter_audio_publish_failed();
    float final_value = param_value_policy_decode_u16(
        &param_registry[parameter_id], value16);
    if (live_parameter_audio_project_sampler_value(
            parameter_id, track, final_value, &final_value) == 0U)
        return live_parameter_audio_publish_failed();
    return control_audio_publish_param(track, parameter_id,
        (uint32_t)live_parameter_event_encode_float(final_value),
        LIVE_PARAMETER_AUDIO_SCOPE_RUNTIME_TEMP, effective_sample_time) != 0U
        ? true : live_parameter_audio_publish_failed();
}
