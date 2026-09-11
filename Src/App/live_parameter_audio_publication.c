#include "App/live_parameter_audio_publication.h"

#include <stddef.h>

#include "IPC/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"
#include "IPC/live_clock_control.h"
#include "IPC/live_parameter_event.h"
#include "Param/param_value_policy.h"
#include "Param/param_registry.h"
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

static uint8_t live_parameter_audio_build_param_command(
    const live_parameter_audio_target_t *target,
    uint64_t effective_sample_time,
    control_audio_command_t *out_command)
{
    if ((target == NULL) || (out_command == NULL)
            || ((target->parameter_id >= PARAM_COUNT)
                && (target->parameter_id < CONTROL_AUDIO_CONFIG_POLY_VOICES))
            || (target->track >= BRICK_ENTITY_CAPACITY))
        return 0U;
    uint8_t kind = 0U;
    if (target->semantic == CONTROL_AUDIO_PARAM_BASE)
    {
        if (target->scope == LIVE_PARAMETER_EVENT_SCOPE_GLOBAL)
        {
            if ((target->track != 0U)
                    || (target->slot != LIVE_PARAMETER_EVENT_INVALID_INDEX))
                return 0U;
            kind = CONTROL_AUDIO_PARAM_KIND_BASE_GLOBAL;
        }
        else if (target->scope == LIVE_PARAMETER_EVENT_SCOPE_TRACK)
        {
            if (target->slot != LIVE_PARAMETER_EVENT_INVALID_INDEX) return 0U;
            kind = CONTROL_AUDIO_PARAM_KIND_BASE_TRACK;
        }
        else if (target->scope == LIVE_PARAMETER_EVENT_SCOPE_SLOT)
        {
            if (target->slot >= 8U) return 0U;
            kind = (uint8_t)(CONTROL_AUDIO_PARAM_KIND_BASE_MATRIX_FIRST
                + target->slot);
        }
        else return 0U;
    }
    else if ((target->semantic == CONTROL_AUDIO_PARAM_TEMP)
            || (target->semantic == CONTROL_AUDIO_PARAM_CLEAR_TEMP))
    {
        if ((target->scope != LIVE_PARAMETER_EVENT_SCOPE_TRACK)
                || (target->slot != LIVE_PARAMETER_EVENT_INVALID_INDEX)
                || (target->parameter_id >= PARAM_COUNT)
                || (param_registry_track_temp_is_applicable(
                    (param_id_t)target->parameter_id, target->track) == 0U)
                || ((target->semantic == CONTROL_AUDIO_PARAM_CLEAR_TEMP)
                    && (param_registry_temp_is_clearable(
                        (param_id_t)target->parameter_id) == 0U))) return 0U;
        kind = (target->semantic == CONTROL_AUDIO_PARAM_TEMP)
            ? CONTROL_AUDIO_PARAM_KIND_TEMP_TRACK
            : CONTROL_AUDIO_PARAM_KIND_CLEAR_TEMP_TRACK;
    }
    else return 0U;
    return control_rt_build_param_command(target->track,
        target->parameter_id, (uint32_t)target->value, kind,
        effective_sample_time, out_command);
}

bool live_parameter_audio_publication_submit_tone_program(
    uint8_t track, track_runtime_type_t type)
{
    if (track >= SEQ_LANE_CAPACITY)
        return live_parameter_audio_publish_failed();
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
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
        bulk.item[bulk.count++] = (live_parameter_audio_target_t){
            .parameter_id = id,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .semantic = CONTROL_AUDIO_PARAM_BASE,
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

bool live_parameter_audio_publication_submit_bulk(
    const live_parameter_audio_bulk_t *bulk)
{
    if ((bulk == NULL) || (bulk->count == 0U)
            || (bulk->count > LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS))
        return live_parameter_audio_publish_failed();
    control_audio_command_t commands[LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS];
    uint64_t sample_time = 0U;
    if (control_rt_capture_tick_to_sample(bulk->capture_tick,
            seq_runtime_exec_get_sample_timeline(), &sample_time) == 0U)
        return live_parameter_audio_publish_failed();
    for (uint8_t i = 0U; i < bulk->count; ++i)
    {
        const live_parameter_audio_target_t *const item = &bulk->item[i];
        for (uint8_t previous = 0U; previous < i; ++previous)
        {
            const live_parameter_audio_target_t *const prior = &bulk->item[previous];
            if ((prior->parameter_id == item->parameter_id)
                    && (prior->scope == item->scope)
                    && (prior->track == item->track)
                    && (prior->slot == item->slot)
                    && (prior->semantic == item->semantic))
                return live_parameter_audio_publish_failed();
        }
        if (live_parameter_audio_build_param_command(
                item, sample_time, &commands[i]) == 0U)
            return live_parameter_audio_publish_failed();
    }
    if (control_rt_publish_batch_scheduled(commands, bulk->count) == 0U)
    {
        /* A valid CONTROL batch is dimensioned before publication.  A refusal
         * is an invariant failure, never a deferred parameter update. */
        Error_Handler();
        return false;
    }
    return true;
}

bool live_parameter_audio_publication_submit(
    uint32_t capture_tick, const live_parameter_audio_target_t *target)
{
    uint64_t sample_time = 0U;
    control_audio_command_t command;
    if ((target == NULL) || (control_rt_capture_tick_to_sample(capture_tick,
            seq_runtime_exec_get_sample_timeline(), &sample_time) == 0U)
            || (live_parameter_audio_build_param_command(
                target, sample_time, &command) == 0U))
        return live_parameter_audio_publish_failed();
    if (control_rt_publish_batch_scheduled(&command, 1U) == 0U)
    {
        Error_Handler();
        return false;
    }
    return true;
}

bool live_parameter_audio_publication_submit_dated(
    uint64_t effective_sample_time, uint16_t parameter_id, uint8_t track,
    uint16_t value16, control_audio_param_semantic_t semantic)
{
    if ((parameter_id >= PARAM_COUNT) || (track >= SEQ_LANE_CAPACITY))
        return live_parameter_audio_publish_failed();
    float final_value = param_value_policy_decode_u16(
        &param_registry[parameter_id], value16);
    const live_parameter_audio_target_t target = {
        .parameter_id = parameter_id,
        .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
        .track = track,
        .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .semantic = (uint8_t)semantic,
        .value = live_parameter_event_encode_float(final_value)
    };
    control_audio_command_t command;
    if ((live_parameter_audio_build_param_command(
            &target, effective_sample_time, &command) == 0U)
            || (control_rt_publish_batch_scheduled(&command, 1U) == 0U))
    {
        Error_Handler();
        return false;
    }
    return true;
}
