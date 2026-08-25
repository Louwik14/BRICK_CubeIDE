#include "Param/param_registry_runtime_state.h"

#include "Storage/memory_layout.h"
#include "Seq/seq_types.h"
#include <string.h>

SEQ_STATE_D2 static float g_param_runtime_track_values[SEQ_LANE_CAPACITY][PARAM_COUNT];
SEQ_STATE_D2 static uint8_t g_param_runtime_track_flags[SEQ_LANE_CAPACITY][PARAM_COUNT];

void param_registry_control_shadow_init(void)
{
    memset(&g_param_runtime_track_values, 0, sizeof(g_param_runtime_track_values));
    memset(&g_param_runtime_track_flags, 0, sizeof(g_param_runtime_track_flags));
}

uint8_t param_registry_control_shadow_ui_value_get(uint8_t track,
                                            param_id_t id,
                                            param_registry_runtime_ui_value_t *out_value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT) || (out_value == NULL))
    {
        return 0U;
    }

    const uint8_t flags = g_param_runtime_track_flags[track][id];
    if ((flags & PARAM_REGISTRY_RUNTIME_UI_VALUE_VALID) == 0U)
    {
        return 0U;
    }

    out_value->base_value = g_param_runtime_track_values[track][id];
    out_value->flags = flags;
    return 1U;
}

uint8_t param_registry_control_shadow_get(uint8_t track, param_id_t id, float *out_value)
{
    param_registry_runtime_ui_value_t value;
    if ((out_value == NULL)
            || (param_registry_control_shadow_ui_value_get(track, id, &value) == 0U))
    {
        return 0U;
    }
    *out_value = value.base_value;
    return 1U;
}

void param_registry_control_shadow_set(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT))
    {
        return;
    }

    g_param_runtime_track_values[track][id] = value;
    g_param_runtime_track_flags[track][id] = PARAM_REGISTRY_RUNTIME_UI_VALUE_VALID;
}

void param_registry_control_shadow_mark_pending(uint8_t track, param_id_t id)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT))
        return;
    g_param_runtime_track_flags[track][id] |=
        (uint8_t)(PARAM_REGISTRY_RUNTIME_UI_VALUE_VALID
                  | PARAM_REGISTRY_RUNTIME_AUDIO_PENDING);
}

void param_registry_control_shadow_set_pending(uint8_t track, param_id_t id, float value)
{
    param_registry_control_shadow_set(track, id, value);
    param_registry_control_shadow_mark_pending(track, id);
}

void param_registry_control_shadow_set_pending_global(param_id_t id, float value)
{
    if (id >= PARAM_COUNT)
        return;
    g_param_runtime_track_values[0U][id] = value;
    g_param_runtime_track_flags[0U][id] =
        (uint8_t)(PARAM_REGISTRY_RUNTIME_UI_VALUE_VALID
                  | PARAM_REGISTRY_RUNTIME_AUDIO_PENDING
                  | PARAM_REGISTRY_RUNTIME_AUDIO_PENDING_GLOBAL);
}

void param_registry_control_shadow_mark_published(uint8_t track, param_id_t id)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT))
        return;
    g_param_runtime_track_flags[track][id] &=
        (uint8_t)~(PARAM_REGISTRY_RUNTIME_AUDIO_PENDING
                   | PARAM_REGISTRY_RUNTIME_AUDIO_PENDING_GLOBAL);
}

uint8_t param_registry_control_shadow_is_pending(uint8_t track, param_id_t id)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT))
        return 0U;
    return (uint8_t)((g_param_runtime_track_flags[track][id]
                      & PARAM_REGISTRY_RUNTIME_AUDIO_PENDING) != 0U);
}

uint8_t param_registry_control_shadow_is_pending_global(param_id_t id)
{
    if (id >= PARAM_COUNT)
        return 0U;
    return (uint8_t)((g_param_runtime_track_flags[0U][id]
                      & PARAM_REGISTRY_RUNTIME_AUDIO_PENDING_GLOBAL) != 0U);
}

void param_registry_control_shadow_clear_track(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return;
    }

    memset(g_param_runtime_track_values[track], 0, sizeof(g_param_runtime_track_values[track]));
    memset(g_param_runtime_track_flags[track], 0, sizeof(g_param_runtime_track_flags[track]));
}

uint8_t param_registry_control_shadow_get_or_default(const param_desc_t *registry,
                                              param_id_t id,
                                              uint8_t track,
                                              float *out_value)
{
    /* Pure query helper: no cache write, no resync, only cache/default resolution. */
    if ((registry == NULL) || (id >= PARAM_COUNT) || (track >= SEQ_LANE_CAPACITY) || (out_value == NULL))
    {
        return 0U;
    }

    if (param_registry_control_shadow_get(track, id, out_value) != 0U)
    {
        return 1U;
    }

    *out_value = registry[id].default_value;
    return 1U;
}
