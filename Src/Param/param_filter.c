#include "Param/param_filter.h"

#include <math.h>
#include <stddef.h>

#include "Platform/memory_layout.h"
#include "Seq/seq_types.h"
#include "Track/track_runtime.h"
#include "IPC/live_clock_control.h"
#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"

CONTROL_STATE_SDRAM static param_filter_control_state_t
    g_param_filter_control[SEQ_LANE_CAPACITY];

static float filter_ui127_clamp(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 127.0f) return 127.0f;
    return value;
}

void param_filter_init(void)
{
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        g_param_filter_control[track] = (param_filter_control_state_t){
            .cutoff = 127.0f,
            .attack = 34.3f,
            .decay = 68.7f,
            .sustain = 127.0f,
            .release = 68.7f,
            .env_reset = 1.0f,
            .retrigger = 1.0f
        };
    }
}

uint8_t param_filter_control_reset(uint8_t track)
{
    const param_filter_control_state_t state = {
        .cutoff = 127.0f, .attack = 34.3f, .decay = 68.7f,
        .sustain = 127.0f, .release = 68.7f, .env_reset = 1.0f,
        .retrigger = 1.0f
    };
    return param_filter_control_restore(track, &state);
}

uint8_t param_filter_is_param(param_id_t id)
{
    return (uint8_t)(((id >= PARAM_FILTER_MORPH)
                      && (id <= PARAM_FILTER_ENVDLY)) ? 1U : 0U);
}

uint8_t param_filter_control_get(uint8_t track, param_id_t id,
                                 float *out_value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (out_value == NULL)) return 0U;
    const param_filter_control_state_t *const state =
        &g_param_filter_control[track];
    switch (id)
    {
        case PARAM_FILTER_MORPH: *out_value = state->morph; return 1U;
        case PARAM_FILTER_CUTOFF: *out_value = state->cutoff; return 1U;
        case PARAM_FILTER_RESONANCE: *out_value = state->resonance; return 1U;
        case PARAM_FILTER_EG_AMT: *out_value = state->eg_amount; return 1U;
        case PARAM_FILTER_ATTACK: *out_value = state->attack; return 1U;
        case PARAM_FILTER_DECAY: *out_value = state->decay; return 1U;
        case PARAM_FILTER_SUSTAIN: *out_value = state->sustain; return 1U;
        case PARAM_FILTER_RELEASE: *out_value = state->release; return 1U;
        case PARAM_FILTER_KEYTRK: *out_value = state->keytrack; return 1U;
        case PARAM_FILTER_ENVRST: *out_value = state->env_reset; return 1U;
        case PARAM_FILTER_ENVDLY: *out_value = state->env_delay; return 1U;
        case PARAM_FILTER_DRIVE: *out_value = state->drive; return 1U;
        case PARAM_FILTER_DECIMATOR_BITS:
            *out_value = state->decimator_bits; return 1U;
        case PARAM_FILTER_DECIMATOR_RATE:
            *out_value = state->decimator_rate; return 1U;
        case PARAM_FILTER_DECIMATOR_RATE2:
            *out_value = state->decimator_rate2; return 1U;
        case PARAM_ENV_RETRIG_FILTER: *out_value = state->retrigger; return 1U;
        default: return 0U;
    }
}

uint8_t param_filter_control_set(uint8_t track, param_id_t id, float value)
{
    if (track >= SEQ_LANE_CAPACITY) return 0U;
    param_filter_control_state_t *const state = &g_param_filter_control[track];
    switch (id)
    {
        case PARAM_FILTER_MORPH: state->morph = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_CUTOFF: state->cutoff = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_RESONANCE: state->resonance = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_EG_AMT: state->eg_amount = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_ATTACK: state->attack = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_DECAY: state->decay = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_SUSTAIN: state->sustain = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_RELEASE: state->release = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_KEYTRK: state->keytrack = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_ENVRST:
            state->env_reset = (value >= 0.5f) ? 1.0f : 0.0f; return 1U;
        case PARAM_FILTER_ENVDLY: state->env_delay = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_DRIVE: state->drive = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_DECIMATOR_BITS:
            state->decimator_bits = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_DECIMATOR_RATE:
            state->decimator_rate = filter_ui127_clamp(value); return 1U;
        case PARAM_FILTER_DECIMATOR_RATE2:
            state->decimator_rate2 = filter_ui127_clamp(value); return 1U;
        case PARAM_ENV_RETRIG_FILTER:
            state->retrigger = (value >= 0.5f) ? 1.0f : 0.0f; return 1U;
        default: return 0U;
    }
}

uint8_t param_filter_control_capture(uint8_t track,
                                     param_filter_control_state_t *out_state)
{
    if ((track >= SEQ_LANE_CAPACITY) || (out_state == NULL)) return 0U;
    *out_state = g_param_filter_control[track];
    return 1U;
}

uint8_t param_filter_control_validate(const param_filter_control_state_t *state)
{
    if (state == NULL) return 0U;
    const float *const values = (const float *)state;
    for (uint8_t i = 0U; i < 16U; ++i)
        if (!isfinite(values[i]) || (values[i] < 0.0f) || (values[i] > 127.0f))
            return 0U;
    return 1U;
}

uint8_t param_filter_control_restore(uint8_t track,
                                     const param_filter_control_state_t *state)
{
    if ((track >= SEQ_LANE_CAPACITY)
            || (param_filter_control_validate(state) == 0U)) return 0U;
    static const param_id_t ids[] = {
        PARAM_FILTER_MORPH, PARAM_FILTER_CUTOFF, PARAM_FILTER_RESONANCE,
        PARAM_FILTER_EG_AMT, PARAM_FILTER_ATTACK, PARAM_FILTER_DECAY,
        PARAM_FILTER_SUSTAIN, PARAM_FILTER_RELEASE, PARAM_FILTER_KEYTRK,
        PARAM_FILTER_ENVRST, PARAM_FILTER_ENVDLY, PARAM_FILTER_DRIVE,
        PARAM_FILTER_DECIMATOR_BITS, PARAM_FILTER_DECIMATOR_RATE,
        PARAM_FILTER_DECIMATOR_RATE2, PARAM_ENV_RETRIG_FILTER
    };
    track_runtime_resolved_track_t resolved;
    if ((track_runtime_resolve_track(track, &resolved) == 0U)
            || (resolved.has_filter_target == 0U)) return 0U;
    param_filter_control_state_t canonical_state = *state;
    float *const values = (float *)&canonical_state;
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(ids) / sizeof(ids[0])); ++i)
    {
        param_registry_prepared_value_t prepared;
        if (param_registry_prepare_value(ids[i], values[i], &prepared) == 0U)
            return 0U;
        values[i] = prepared.value;
    }
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = (uint8_t)(sizeof(ids) / sizeof(ids[0]))
    };
    for (uint8_t i = 0U; i < bulk.count; ++i)
        bulk.item[i] = (live_parameter_audio_bulk_item_t){
            .parameter_id = (uint16_t)ids[i],
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
            .value = live_parameter_event_encode_float(values[i])
        };
    if (!live_parameter_audio_publication_submit_bulk(&bulk)) return 0U;
    g_param_filter_control[track] = canonical_state;
    return 1U;
}

uint8_t param_filter_apply_value(param_id_t id, uint8_t track, float clamped,
                                 uint8_t update_control_value)
{
    track_runtime_resolved_track_t resolved;
    if ((track_runtime_resolve_track(track, &resolved) == 0U)
            || (resolved.has_filter_target == 0U)) return 0U;
    if (param_registry_publish_track_base_audio(id, track, clamped) == 0U)
        return 0U;
    return (update_control_value == 0U)
        ? 1U : param_filter_control_set(track, id, clamped);
}
