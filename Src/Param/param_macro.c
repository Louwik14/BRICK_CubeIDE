#include "Param/param_macro.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "Param/param_filter.h"
#include "Param/param_registry_backends.h"
#include "Param/param_registry.h"
#include "Seq/seq_param_iface.h"
#include "Storage/memory_layout.h"
#include "Storage/project_v1.h"

typedef struct
{
    float amount;
    uint8_t scene;
    uint8_t active;
    uint32_t touch_seq;
    uint8_t last_count;
    param_macro_resolution_t last_resolution[PROJECT_V1_MACRO_SCENE_LOCK_COUNT];
} param_macro_source_state_t;

#define PARAM_MACRO_POT_SOURCE_COUNT PROJECT_V1_MACRO_POT_COUNT
#define PARAM_MACRO_HALL_SOURCE_COUNT PROJECT_V1_MACRO_SCENE_COUNT
#define PARAM_MACRO_SOURCE_COUNT (PARAM_MACRO_POT_SOURCE_COUNT + PARAM_MACRO_HALL_SOURCE_COUNT)

CONTROL_STATE_SDRAM static param_macro_source_state_t g_param_macro_sources[PARAM_MACRO_SOURCE_COUNT];
static uint32_t g_param_macro_touch_seq;

uint8_t param_macro_resolve_lock(uint8_t scene,
                                 uint8_t lock,
                                 param_macro_resolution_t *out_resolution);
uint8_t param_macro_apply_resolution(const param_macro_resolution_t *resolution);

__attribute__((weak)) uint8_t param_macro_get_ui_held_scene(uint8_t macro, uint8_t *out_scene)
{
    (void)macro;
    (void)out_scene;
    return 0U;
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
        case TRACK_RUNTIME_PARAM_DOMAIN_COLORS:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_COLORS;
            return 1U;
        case TRACK_RUNTIME_PARAM_DOMAIN_TONE:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_TONE;
            return 1U;
        case TRACK_RUNTIME_PARAM_DOMAIN_PLAY:
            *out_set_id = (uint8_t)SEQ_PLOCK_SET_PLAY;
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
    memset(g_param_macro_sources, 0, sizeof(g_param_macro_sources));
    g_param_macro_touch_seq = 0U;
    for (uint8_t macro = 0U; macro < PROJECT_V1_MACRO_POT_COUNT; ++macro)
    {
        g_param_macro_sources[macro].scene = project_v1_macro_get_macro_scene(macro);
    }

    for (uint8_t scene = 0U; scene < PROJECT_V1_MACRO_SCENE_COUNT; ++scene)
    {
        g_param_macro_sources[PARAM_MACRO_POT_SOURCE_COUNT + scene].scene = scene;
    }
}

float param_macro_lerp(float base_value, float scene_value, float amount)
{
    if (amount <= 0.0f)
    {
        return base_value;
    }

    if (amount >= 1.0f)
    {
        return scene_value;
    }

    return base_value + ((scene_value - base_value) * amount);
}

uint8_t param_macro_slot_target_is_supported(uint8_t track, param_id_t param)
{
    if ((track >= SEQ_TRACK_COUNT) || (param >= PARAM_COUNT))
    {
        return 0U;
    }

    {
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
        uint8_t set_id = 0U;
        if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
        {
            return 0U;
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

static uint8_t param_macro_apply_preview_value(uint8_t track, param_id_t param, float value)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    track_runtime_resolved_track_t resolved;

    if (param_macro_slot_target_is_supported(track, param) == 0U)
    {
        return 0U;
    }

    if (param_filter_is_param(param) != 0U)
    {
        return param_filter_apply_value(param, track, value, 0U, 0U);
    }

    if (track_runtime_resolve_track(track, &resolved) == 0U)
    {
        return 0U;
    }

    if (resolved.descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
    {
        return 0U;
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (param_backend_track_supports_midi_tone_descriptor(&resolved.descriptor) != 0U))
    {
        if (param == PARAM_MIDI_PROGRAM)
        {
            return param_registry_apply_track_value(param, track, value);
        }

        if (param_backend_is_midi_cc_id(param) == 0U)
        {
            return 0U;
        }

        return param_backend_send_midi_cc(track, param, value);
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD))
    {
        return param_registry_apply_track_value(param, track, value);
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
    {
        return param_backend_apply_mix_track(track_runtime_get_ctx(track), track, param, value, 0U);
    }

    return param_backend_apply_track_value(track, param, value, 0U);
}

static void param_macro_release_source(param_macro_source_state_t *source)
{
    if ((source == NULL) || (source->last_count == 0U))
    {
        return;
    }

    for (uint8_t i = 0U; i < source->last_count; ++i)
    {
        const param_macro_resolution_t *const last = &source->last_resolution[i];
        if ((last->track >= SEQ_TRACK_COUNT)
                || (last->param >= PARAM_COUNT)
                || (last->resolved_value == last->base_value))
        {
            continue;
        }

        (void)param_macro_apply_preview_value(last->track, last->param, last->base_value);
    }

    memset(source->last_resolution, 0, sizeof(source->last_resolution));
    source->last_count = 0U;
}

static uint8_t param_macro_apply_source(param_macro_source_state_t *source)
{
    uint8_t any_applied = 0U;

    if ((source == NULL) || (source->active == 0U) || (source->amount <= 0.0f))
    {
        return 0U;
    }

    for (uint8_t lock = 0U; lock < PROJECT_V1_MACRO_SCENE_LOCK_COUNT; ++lock)
    {
        param_macro_resolution_t resolution;
        if (param_macro_resolve_lock(source->scene, lock, &resolution) == 0U)
        {
            continue;
        }

        resolution.amount = source->amount;
        resolution.resolved_value = param_macro_lerp(resolution.base_value, resolution.scene_value, source->amount);
        if (param_macro_apply_resolution(&resolution) == 0U)
        {
            continue;
        }

        if (source->last_count < PROJECT_V1_MACRO_SCENE_LOCK_COUNT)
        {
            source->last_resolution[source->last_count++] = resolution;
        }
        any_applied = 1U;
    }

    return any_applied;
}

static void param_macro_recompute_sources(void)
{
    uint32_t last_applied_seq = 0U;

    for (uint8_t source = 0U; source < PARAM_MACRO_SOURCE_COUNT; ++source)
    {
        param_macro_release_source(&g_param_macro_sources[source]);
    }

    for (;;)
    {
        uint8_t best = PARAM_MACRO_SOURCE_COUNT;
        uint32_t best_seq = 0xFFFFFFFFUL;
        for (uint8_t source = 0U; source < PARAM_MACRO_SOURCE_COUNT; ++source)
        {
            const param_macro_source_state_t *const s = &g_param_macro_sources[source];
            if ((s->active == 0U) || (s->amount <= 0.0f) || (s->touch_seq <= last_applied_seq))
            {
                continue;
            }

            if (s->touch_seq < best_seq)
            {
                best_seq = s->touch_seq;
                best = source;
            }
        }

        if (best >= PARAM_MACRO_SOURCE_COUNT)
        {
            break;
        }

        (void)param_macro_apply_source(&g_param_macro_sources[best]);
        last_applied_seq = best_seq;
    }
}

static uint8_t param_macro_set_source_amount(uint8_t source_index, uint8_t scene, float amount)
{
    const float clamped = param_macro_clamp_amount(amount);
    const uint8_t active = (clamped > 0.0f) ? 1U : 0U;

    if ((source_index >= PARAM_MACRO_SOURCE_COUNT) || (scene >= PROJECT_V1_MACRO_SCENE_COUNT))
    {
        return 0U;
    }

    param_macro_source_state_t *const source = &g_param_macro_sources[source_index];
    if ((source->scene == scene) && (source->amount == clamped) && (source->active == active))
    {
        return source->active;
    }

    source->scene = scene;
    source->amount = clamped;
    source->active = active;
    source->touch_seq = ++g_param_macro_touch_seq;
    param_macro_recompute_sources();
    return source->active;
}

uint8_t param_macro_resolve_lock(uint8_t scene,
                                 uint8_t lock,
                                 param_macro_resolution_t *out_resolution)
{
    project_v1_macro_lock_t macro_lock;
    float base_value = 0.0f;

    if (out_resolution == NULL)
    {
        return 0U;
    }

    memset(out_resolution, 0, sizeof(*out_resolution));

    if (project_v1_macro_get_scene_lock(scene, lock, &macro_lock) == 0U)
    {
        return 0U;
    }

    if ((macro_lock.track == PROJECT_V1_MACRO_LOCK_TRACK_NONE)
            || (macro_lock.param == PROJECT_V1_MACRO_LOCK_PARAM_NONE)
            || (param_macro_slot_target_is_supported(macro_lock.track, macro_lock.param) == 0U))
    {
        return 0U;
    }

    if (param_registry_get_track_value(macro_lock.param, macro_lock.track, &base_value) == 0U)
    {
        return 0U;
    }

    out_resolution->scene = scene;
    out_resolution->lock = lock;
    out_resolution->track = macro_lock.track;
    out_resolution->param = macro_lock.param;
    out_resolution->base_value = base_value;
    out_resolution->scene_value = macro_lock.scene_value;
    out_resolution->amount = 0.0f;
    out_resolution->resolved_value = base_value;
    return 1U;
}

uint8_t param_macro_resolve_slot(uint8_t bank,
                                 uint8_t macro,
                                 uint8_t slot,
                                 param_macro_resolution_t *out_resolution)
{
    (void)macro;
    return param_macro_resolve_lock(bank, slot, out_resolution);
}

uint8_t param_macro_apply_resolution(const param_macro_resolution_t *resolution)
{
    if ((resolution == NULL)
            || (resolution->track >= SEQ_TRACK_COUNT)
            || (resolution->param >= PARAM_COUNT)
            || (param_macro_slot_target_is_supported(resolution->track, resolution->param) == 0U))
    {
        return 0U;
    }

    return param_macro_apply_preview_value(resolution->track,
                                           resolution->param,
                                           resolution->resolved_value);
}

void param_macro_sync_active_bank(void)
{
    for (uint8_t macro = 0U; macro < PROJECT_V1_MACRO_POT_COUNT; ++macro)
    {
        g_param_macro_sources[macro].scene = project_v1_macro_get_macro_scene(macro);
    }
    param_macro_recompute_sources();
}

uint8_t param_macro_set_amount(uint8_t macro, float amount)
{
    uint8_t held_scene = 0U;

    if (macro >= PROJECT_V1_MACRO_POT_COUNT)
    {
        return 0U;
    }

    if (param_macro_get_ui_held_scene(macro, &held_scene) != 0U)
    {
        project_v1_macro_set_macro_scene_no_sync(macro, held_scene);
        g_param_macro_sources[macro].scene = project_v1_macro_get_macro_scene(macro);
        return 1U;
    }

    return param_macro_set_source_amount(macro, project_v1_macro_get_macro_scene(macro), amount);
}

uint8_t param_macro_adjust_amount(uint8_t macro, int16_t delta)
{
    float next_amount = 0.0f;

    if (macro >= PROJECT_V1_MACRO_POT_COUNT)
    {
        return 0U;
    }

    next_amount = g_param_macro_sources[macro].amount + ((float)delta * 0.015625f);
    return param_macro_set_amount(macro, next_amount);
}

float param_macro_get_amount(uint8_t macro)
{
    if (macro >= PROJECT_V1_MACRO_POT_COUNT)
    {
        return 0.0f;
    }

    return g_param_macro_sources[macro].amount;
}

uint8_t param_macro_set_scene_source_amount(uint8_t scene, float amount)
{
    if (scene >= PROJECT_V1_MACRO_SCENE_COUNT)
    {
        return 0U;
    }

    return param_macro_set_source_amount((uint8_t)(PARAM_MACRO_POT_SOURCE_COUNT + scene), scene, amount);
}

void param_macro_release_scene_source(uint8_t scene)
{
    if (scene >= PROJECT_V1_MACRO_SCENE_COUNT)
    {
        return;
    }

    (void)param_macro_set_source_amount((uint8_t)(PARAM_MACRO_POT_SOURCE_COUNT + scene), scene, 0.0f);
}

uint8_t param_macro_apply_slot(uint8_t bank, uint8_t macro, uint8_t slot, float amount)
{
    param_macro_resolution_t resolution;
    float clamped_amount = amount;

    if (param_macro_resolve_slot(bank, macro, slot, &resolution) == 0U)
    {
        return 0U;
    }

    if (clamped_amount < 0.0f)
    {
        clamped_amount = 0.0f;
    }
    else if (clamped_amount > 1.0f)
    {
        clamped_amount = 1.0f;
    }

    resolution.amount = clamped_amount;
    resolution.resolved_value = param_macro_lerp(resolution.base_value, resolution.scene_value, clamped_amount);
    return param_macro_apply_resolution(&resolution);
}
