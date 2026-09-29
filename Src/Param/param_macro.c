#include "Param/param_macro.h"

#include <string.h>

#include "Platform/brick_media_clock.h"
#include "IPC/control_audio_command.h"
#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"
#include "Track/track_runtime.h"
#include "Param/param_filter.h"
#include "Param/param_control_backends.h"
#include "Param/param_registry.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Seq/seq_param_iface.h"
#include "Platform/memory_layout.h"
#include "Storage/project_control.h"
#include "Storage/persistent_key_catalog.h"

#define PARAM_MACRO_TARGET_CAPACITY \
    (2U * PERSIST_CONTROL_MACRO_COUNT * PERSIST_CONTROL_MACRO_LOCK_COUNT)

typedef struct
{
    uint8_t track;
    param_id_t param;
    float base;
    float delta;
    uint8_t active;
    uint8_t audio;
} param_macro_target_t;

static float g_macro_amounts[PERSIST_CONTROL_MACRO_COUNT];
CONTROL_STATE_SDRAM static param_macro_target_t g_macro_previous[PARAM_MACRO_TARGET_CAPACITY];
CONTROL_STATE_SDRAM static param_macro_target_t g_macro_work[PARAM_MACRO_TARGET_CAPACITY];
static uint16_t g_macro_previous_count;
static uint8_t g_macro_retry_pending;

static uint8_t param_macro_prepare_temp_target(param_id_t param,
                                               uint8_t track,
                                               float value,
                                               live_parameter_audio_target_t *out,
                                               float *out_canonical_value)
{
    if ((out == NULL) || (param >= PARAM_COUNT) || (track >= SEQ_LANE_CAPACITY)
            || (out_canonical_value == NULL)
            || (param_registry_track_temp_is_applicable(param, track) == 0U))
    {
        return 0U;
    }

    uint8_t event_track = track;
    if (param_registry_is_lfo_param(param) != 0U)
    {
        const uint8_t offset = (uint8_t)(param - PARAM_LFO1_RATE);
        const uint8_t lfo_index = (uint8_t)(offset / MOD_LFO_PARAM_COUNT);
        const mod_lfo_param_t lfo_param =
            (mod_lfo_param_t)(offset % MOD_LFO_PARAM_COUNT);
        if (mod_lfo_v1_prepare_track_param(track, lfo_index, lfo_param,
                value, &event_track, &value) == 0U) return 0U;
    }
    else
    {
        param_registry_prepared_value_t prepared;
        value = param_value_policy_canonicalize(param, track, value);
        if ((track_runtime_get_effective_param_status(track, param)
                != TRACK_RUNTIME_PARAM_ALLOWED)
                || (param_registry_prepare_value(param, value, &prepared) == 0U))
            return 0U;
        value = prepared.value;
    }
    *out_canonical_value = value;
    *out = (live_parameter_audio_target_t){
        .parameter_id = (uint16_t)param,
        .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
        .track = event_track,
        .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .semantic = CONTROL_AUDIO_PARAM_TEMP,
        .value = live_parameter_event_encode_float(value)
    };
    return 1U;
}

static uint8_t param_macro_bulk_add(live_parameter_audio_bulk_t *bulk,
                                    param_id_t param,
                                    uint8_t track,
                                    float value,
                                    float *out_canonical_value)
{
    live_parameter_audio_target_t target;
    if ((bulk == NULL) || (param_macro_prepare_temp_target(param, track, value,
            &target, out_canonical_value) == 0U)) return 0U;

    for (uint8_t i = 0U; i < bulk->count; ++i)
    {
        live_parameter_audio_target_t *const item = &bulk->item[i];
        if ((item->parameter_id == target.parameter_id)
                && (item->scope == target.scope)
                && (item->track == target.track)
                && (item->slot == target.slot)
                && (item->semantic == target.semantic))
        {
            *item = target;
            return 1U;
        }
    }

    if (bulk->count >= LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS)
    {
        return 0U;
    }

    bulk->item[bulk->count++] = target;
    return 1U;
}

static uint8_t param_macro_bulk_add_clear_temp(
    live_parameter_audio_bulk_t *bulk, param_id_t param, uint8_t track)
{
    if ((bulk == NULL) || (track >= SEQ_LANE_CAPACITY)
            || (param_registry_temp_is_clearable(param) == 0U)) return 0U;
    for (uint8_t i = 0U; i < bulk->count; ++i)
    {
        const live_parameter_audio_target_t *const item = &bulk->item[i];
        if ((item->parameter_id == (uint16_t)param)
                && (item->track == track)
                && (item->semantic == CONTROL_AUDIO_PARAM_CLEAR_TEMP)) return 1U;
    }
    if (bulk->count >= LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS) return 0U;
    bulk->item[bulk->count++] = (live_parameter_audio_target_t){
        .parameter_id = (uint16_t)param,
        .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
        .track = track,
        .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
        .semantic = CONTROL_AUDIO_PARAM_CLEAR_TEMP,
        .value = 0
    };
    return 1U;
}

static float param_macro_clamp_amount(float amount)
{
    if (amount < 0.0f)
    {
        return 0.0f;
    }

    if (amount > 1.0f)
    {
        return 1.0f;
    }

    return amount;
}

static uint8_t param_macro_plock_set_for_domain(track_runtime_param_domain_t domain, uint8_t *out_set_id)
{
    if (out_set_id == NULL)
    {
        return 0U;
    }

    switch (domain)
    {
        case TRACK_RUNTIME_PARAM_DOMAIN_CFG:
            return 0U;
        case TRACK_RUNTIME_PARAM_DOMAIN_ENV:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_ENV;
            return 1U;
        case TRACK_RUNTIME_PARAM_DOMAIN_TONE:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_TONE;
            return 1U;
        case TRACK_RUNTIME_PARAM_DOMAIN_MOD:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_MOD;
            return 1U;
        case TRACK_RUNTIME_PARAM_DOMAIN_MIX:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_MIX;
            return 1U;
        default:
            return 0U;
    }
}

void param_macro_init(void)
{
    memset(g_macro_amounts, 0, sizeof(g_macro_amounts));
    g_macro_previous_count = 0U;
    g_macro_retry_pending = 0U;
    memset(g_macro_previous, 0, sizeof(g_macro_previous));
}

uint8_t param_macro_lock_target_is_supported(uint8_t track, param_id_t param)
{
    persist_param_descriptor_t descriptor;
    if ((track >= SEQ_LANE_CAPACITY) || (param >= PARAM_COUNT))
        return 0U;
    if ((persist_key_param_descriptor(param, &descriptor) == 0U)
            || (descriptor.key == 0U)
            || (descriptor.scope != PERSIST_PARAM_SCOPE_ENTITY)
            || (descriptor.kind != PERSIST_VALUE_FLOAT32))
        return 0U;

    const param_desc_t *const desc = &param_registry[param];
    param_value_policy_t policy;
    if ((param_value_policy_resolve(param, track, &policy) == 0U)
            || (desc->id != param) || (desc->type > PARAM_TYPE_BIPOLAR)
            || (policy.canonical_to_display == NULL)
            || (policy.display_to_canonical == NULL)
            || (policy.automation > PARAM_AUTOMATION_LINEAR_U16)
            || (track_runtime_get_effective_param_status(track, param)
                != TRACK_RUNTIME_PARAM_ALLOWED))
        return 0U;

    {
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
        uint8_t set_id = 0U;
        if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
        {
            return 0U;
        }
        if ((param == PARAM_MIDI_PROGRAM)
                && (param_backend_track_supports_midi_tone_ctx(
                    track_runtime_get_ctx(track)) != 0U))
        {
            return 0U;
        }

        if ((param >= PARAM_FM_OPERATOR_FIRST) && (param <= PARAM_FM_OPERATOR_LAST))
        {
            set_id = (uint8_t)SEQ_PLOCK_SET_FM_OPERATOR;
            return seq_param_iface_param_is_supported(track, set_id, param);
        }

        if (param_macro_plock_set_for_domain(rule.domain, &set_id) != 0U)
        {
            return seq_param_iface_param_is_supported(track, set_id, param);
        }

        if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
        {
            return (track_runtime_get_effective_param_status(track, param) == TRACK_RUNTIME_PARAM_ALLOWED) ? 1U : 0U;
        }

        return 0U;
    }
}

static uint8_t param_macro_apply_backend_value(uint8_t track, param_id_t param, float value)
{
    track_runtime_resolved_track_t resolved;

    if ((param_registry_track_temp_is_applicable(param, track) != 0U)
            || (param_macro_lock_target_is_supported(track, param) == 0U))
    {
        return 0U;
    }

    if (track_runtime_resolve_track(track, &resolved) == 0U)
    {
        return 0U;
    }

    if (resolved.descriptor.active == 0U)
    {
        return 0U;
    }

    if ((param_backend_track_supports_midi_tone_descriptor(
                &resolved.descriptor) == 0U)
            || (param_backend_is_midi_cc_id(param) == 0U)) return 0U;
    param_registry_prepared_value_t prepared;
    value = param_value_policy_canonicalize(param, track, value);
    if (param_registry_prepare_value(param, value, &prepared) == 0U) return 0U;
    return param_backend_send_midi_cc(track, param, prepared.value);
}

static uint8_t param_macro_collect_value(live_parameter_audio_bulk_t *bulk,
                                         uint8_t track,
                                         param_id_t param,
                                         float *value)
{
    if (value == NULL) return 0U;
    if (param_registry_track_temp_is_applicable(param, track) != 0U)
    {
        return param_macro_bulk_add(bulk, param, track, *value, value);
    }
    if ((param_macro_lock_target_is_supported(track, param) == 0U)
            || (param_backend_is_midi_cc_id(param) == 0U)) return 0U;
    param_registry_prepared_value_t prepared;
    *value = param_value_policy_canonicalize(param, track, *value);
    if (param_registry_prepare_value(param, *value, &prepared) == 0U) return 0U;
    *value = prepared.value;
    return 1U;
}

static uint8_t param_macro_flush_bulk(live_parameter_audio_bulk_t *bulk)
{
    if (bulk->count == 0U) return 1U;
    if (live_parameter_audio_publication_submit_bulk(bulk) == false) return 0U;
    bulk->count = 0U;
    bulk->capture_tick = brick_media_clock_now_tick();
    return 1U;
}

static uint8_t param_macro_recompute(const float amounts[PERSIST_CONTROL_MACRO_COUNT])
{
    uint16_t count = 0U;
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = brick_media_clock_now_tick(), .count = 0U
    };

    /* Include old targets so a released macro clears its temporary value. */
    for (uint16_t i = 0U; i < g_macro_previous_count; ++i)
    {
        g_macro_work[count] = g_macro_previous[i];
        g_macro_work[count].active = 0U;
        g_macro_work[count].delta = 0.0f;
        ++count;
    }

    for (uint8_t macro = 0U; macro < PERSIST_CONTROL_MACRO_COUNT; ++macro)
    {
        if (amounts[macro] <= 0.0f) continue;
        for (uint8_t lock = 0U; lock < PERSIST_CONTROL_MACRO_LOCK_COUNT; ++lock)
        {
            if (project_control_macro_lock_is_empty(macro, lock) != 0U) break;
            project_control_macro_lock_t entry;
            if (project_control_get_macro_lock(macro, lock, &entry) == 0U) return 0U;
            if (param_macro_lock_target_is_supported(entry.track, entry.param) == 0U)
                continue;
            uint16_t index = count;
            for (uint16_t i = 0U; i < count; ++i)
                if (g_macro_work[i].track == entry.track
                    && g_macro_work[i].param == entry.param) { index = i; break; }
            if (index == count)
            {
                if (count >= PARAM_MACRO_TARGET_CAPACITY) return 0U;
                float base = 0.0f;
                if (param_registry_get_track_value(entry.param, entry.track, &base) == 0U)
                    return 0U;
                g_macro_work[index] = (param_macro_target_t){
                    .track = entry.track, .param = entry.param,
                    .base = base, .delta = 0.0f, .active = 0U,
                    .audio = param_registry_track_temp_is_applicable(entry.param, entry.track)
                };
                ++count;
            }
            else if (g_macro_work[index].active == 0U)
            {
                if (param_registry_get_track_value(entry.param, entry.track,
                                                   &g_macro_work[index].base) == 0U) return 0U;
            }
            g_macro_work[index].delta += amounts[macro]
                * (entry.target_value - g_macro_work[index].base);
            g_macro_work[index].active = 1U;
        }
    }

    for (uint16_t i = 0U; i < count; ++i)
    {
        param_macro_target_t *target = &g_macro_work[i];
        const uint8_t audio = target->active != 0U
            ? param_registry_track_temp_is_applicable(target->param, target->track)
            : target->audio;
        if (audio != 0U)
        {
            if (bulk.count >= LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS
                && param_macro_flush_bulk(&bulk) == 0U) return 0U;
            if (target->active == 0U && param_registry_temp_is_clearable(target->param) != 0U)
            {
                if (param_macro_bulk_add_clear_temp(&bulk, target->param,
                                                    target->track) == 0U) return 0U;
            }
            else
            {
                float value = target->base + target->delta;
                if (value < param_registry[target->param].min) value = param_registry[target->param].min;
                if (value > param_registry[target->param].max) value = param_registry[target->param].max;
                if (param_macro_collect_value(&bulk, target->track,
                                              target->param, &value) == 0U) return 0U;
            }
        }
        else
        {
            if (target->active == 0U
                && param_macro_lock_target_is_supported(target->track, target->param) == 0U)
                continue;
            float value = target->base + target->delta;
            if (value < param_registry[target->param].min) value = param_registry[target->param].min;
            if (value > param_registry[target->param].max) value = param_registry[target->param].max;
            if (param_macro_apply_backend_value(target->track, target->param, value) == 0U)
                return 0U;
        }
    }
    if (param_macro_flush_bulk(&bulk) == 0U) return 0U;
    g_macro_previous_count = 0U;
    for (uint16_t i = 0U; i < count; ++i)
        if (g_macro_work[i].active != 0U)
            g_macro_previous[g_macro_previous_count++] = g_macro_work[i];
    return 1U;
}

uint8_t param_macro_sync_sources(void)
{
    const uint8_t ok = param_macro_recompute(g_macro_amounts);
    g_macro_retry_pending = (ok == 0U) ? 1U : 0U;
    return ok;
}

void param_macro_service(void)
{
    if (g_macro_retry_pending != 0U)
        (void)param_macro_sync_sources();
}

void param_macro_note_base_change(uint8_t track, param_id_t param)
{
    for (uint16_t i = 0U; i < g_macro_previous_count; ++i)
        if (g_macro_previous[i].track == track && g_macro_previous[i].param == param)
        {
            (void)param_macro_sync_sources();
            return;
        }
}

uint8_t param_macro_set_amount(uint8_t macro, float amount)
{
    if (macro >= PERSIST_CONTROL_MACRO_COUNT) return 0U;
    amount = param_macro_clamp_amount(amount);
    if (amount == g_macro_amounts[macro]) return 1U;
    float candidate[PERSIST_CONTROL_MACRO_COUNT];
    memcpy(candidate, g_macro_amounts, sizeof(candidate));
    candidate[macro] = amount;
    const uint8_t ok = param_macro_recompute(candidate);
    memcpy(g_macro_amounts, candidate, sizeof(candidate));
    g_macro_retry_pending = (ok == 0U) ? 1U : 0U;
    return ok;
}

float param_macro_get_amount(uint8_t macro)
{
    return macro < PERSIST_CONTROL_MACRO_COUNT ? g_macro_amounts[macro] : 0.0f;
}

void param_macro_reset(void)
{
    float cleared[PERSIST_CONTROL_MACRO_COUNT] = {0.0f};
    const uint8_t ok = param_macro_recompute(cleared);
    memset(g_macro_amounts, 0, sizeof(g_macro_amounts));
    g_macro_retry_pending = (ok == 0U) ? 1U : 0U;
}
