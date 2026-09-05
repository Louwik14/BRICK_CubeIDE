#include "App/live_parameter_audio_publication.h"

#include <stddef.h>

#include "IPC/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"
#include "IPC/live_clock_control.h"
#include "IPC/live_parameter_event.h"
#include "Param/param_value_policy.h"
#include "Track/track_runtime.h"
#include "Track/tone_param_codec.h"
#include "Track/tone_program_control.h"
#include "Seq/seq_runtime_exec.h"
#include "main.h"

static uint32_t g_live_parameter_audio_publish_failure_count;

static bool live_parameter_audio_publish_failed(void)
{
    ++g_live_parameter_audio_publish_failure_count;
    return false;
}

static uint8_t live_parameter_audio_make_command(
    uint16_t parameter_id, uint8_t scope,
    uint8_t track, uint8_t slot, uint16_t flags, int32_t raw_value,
    control_audio_command_t *out_command)
{
    if ((out_command == NULL)
            || ((parameter_id >= PARAM_COUNT)
                && (parameter_id < CONTROL_AUDIO_CONFIG_POLY_VOICES)))
        return 0U;
    float value = ((flags & LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS) != 0U)
        ? live_parameter_event_decode_float(raw_value) : (float)raw_value;
    uint8_t command_scope = scope;
    if ((flags & LIVE_PARAMETER_EVENT_FLAG_RUNTIME_TEMP) != 0U)
    {
        command_scope = LIVE_PARAMETER_AUDIO_SCOPE_RUNTIME_TEMP;
    }
    else if (scope == LIVE_PARAMETER_EVENT_SCOPE_SLOT)
    {
        if (slot >= 8U) return 0U;
        command_scope = (uint8_t)(LIVE_PARAMETER_AUDIO_SCOPE_MATRIX_SLOT_BASE + slot);
    }
    *out_command = (control_audio_command_t){
        .value = (uint32_t)live_parameter_event_encode_float(value),
        .id = parameter_id,
        .entity = (track < BRICK_ENTITY_CAPACITY) ? track : 0U,
        .opcode_kind = CONTROL_AUDIO_COMMAND_TAG(
            CONTROL_AUDIO_COMMAND_PARAM, command_scope & 0x1FU)
    };
    return 1U;
}

static bool live_parameter_audio_build_commands(
    const live_parameter_audio_bulk_t *bulk,
    control_audio_command_t commands[LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS])
{
    if ((bulk == NULL) || (commands == NULL) || (bulk->count == 0U)
            || (bulk->count > LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS))
        return false;
    for (uint8_t i = 0U; i < bulk->count; ++i)
    {
        const live_parameter_audio_bulk_item_t *const item = &bulk->item[i];
        for (uint8_t previous = 0U; previous < i; ++previous)
        {
            const live_parameter_audio_bulk_item_t *const prior = &bulk->item[previous];
            if ((prior->parameter_id == item->parameter_id)
                    && (prior->scope == item->scope)
                    && (prior->track == item->track)
                    && (prior->slot == item->slot)
                    && ((item->parameter_id != CONTROL_AUDIO_PARAM_CLEAR_RUNTIME_TEMP)
                        || (prior->value == item->value)))
                return false;
        }
        if (live_parameter_audio_make_command(
                item->parameter_id, item->scope, item->track,
                item->slot, item->flags, item->value, &commands[i]) == 0U)
            return false;
    }
    return true;
}

void live_parameter_audio_publication_init(void)
{
    g_live_parameter_audio_publish_failure_count = 0U;
}

bool live_parameter_audio_publication_submit_bulk(
    const live_parameter_audio_bulk_t *bulk)
{
    if ((bulk == NULL) || (bulk->count == 0U)
            || (bulk->count > LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS))
        return live_parameter_audio_publish_failed();
    control_audio_command_t commands[LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS];
    if (!live_parameter_audio_build_commands(bulk, commands))
        return live_parameter_audio_publish_failed();
    if (control_rt_publish_batch_captured(
        commands, bulk->count, bulk->capture_tick,
        seq_runtime_exec_get_sample_timeline()) == 0U)
    {
        /* A valid CONTROL batch is dimensioned before publication.  A refusal
         * is an invariant failure, never a deferred parameter update. */
        Error_Handler();
        return false;
    }
    return true;
}

bool live_parameter_audio_publication_submit_bulk_now(
    const live_parameter_audio_bulk_t *bulk)
{
    if ((bulk == NULL) || (bulk->count == 0U)
            || (bulk->count > LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS))
        return live_parameter_audio_publish_failed();
    control_audio_command_t commands[LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS];
    if (!live_parameter_audio_build_commands(bulk, commands))
        return live_parameter_audio_publish_failed();
    if (control_rt_publish_batch_now(commands, bulk->count) == 0U)
    {
        Error_Handler();
        return false;
    }
    return true;
}

bool live_parameter_audio_publication_submit_bulk_scheduled(
    const live_parameter_audio_bulk_t *bulk, uint64_t effective_sample_time)
{
    if ((bulk == NULL) || (bulk->count == 0U)
            || (bulk->count > LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS))
        return live_parameter_audio_publish_failed();
    control_audio_command_t commands[LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS];
    if (!live_parameter_audio_build_commands(bulk, commands))
        return live_parameter_audio_publish_failed();
    for (uint8_t i = 0U; i < bulk->count; ++i)
        commands[i].effective_sample_time = effective_sample_time;
    if (control_rt_publish_batch_scheduled(commands, bulk->count) == 0U)
    {
        Error_Handler();
        return false;
    }
    return true;
}

bool live_parameter_audio_publication_submit_tone_program_scheduled(
    uint8_t track, track_runtime_type_t type, uint64_t effective_sample_time)
{
    if (track >= SEQ_LANE_CAPACITY)
        return live_parameter_audio_publish_failed();
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = 0U,
        .count = 0U
    };
    const uint8_t count = tone_param_codec_count(type);
    for (uint8_t slot = 0U; slot < count; ++slot)
    {
        param_id_t id = PARAM_COUNT;
        float value;
        if ((tone_param_codec_slot_to_param(type, slot, &id) == 0U)
                || (tone_program_control_get(track, id, &value) == 0U)
                || (param_registry_track_value_is_audio_command(id, track) == 0U))
            continue;
        bulk.item[bulk.count++] = (live_parameter_audio_bulk_item_t){
            .parameter_id = id,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
            .value = live_parameter_event_encode_float(value)
        };
    }
    if (bulk.count == 0U) return true;
    return live_parameter_audio_publication_submit_bulk_scheduled(
        &bulk, effective_sample_time);
}

bool live_parameter_audio_publication_submit_dated(
    uint64_t effective_sample_time, uint16_t parameter_id, uint8_t track,
    uint16_t value16)
{
    if ((parameter_id >= PARAM_COUNT) || (track >= SEQ_LANE_CAPACITY))
        return live_parameter_audio_publish_failed();
    float final_value = param_value_policy_decode_u16(
        &param_registry[parameter_id], value16);
    if (control_rt_publish_param(track, parameter_id,
        (uint32_t)live_parameter_event_encode_float(final_value),
        LIVE_PARAMETER_AUDIO_SCOPE_RUNTIME_TEMP, effective_sample_time) == 0U)
    {
        Error_Handler();
        return false;
    }
    return true;
}
