#include "Param/param_registry_runtime_state.h"

#include "Mod/mod_lfo_v1.h"
#include "Storage/memory_layout.h"
#include <string.h>

SEQ_STATE_D2 static float g_param_runtime_track_values[SEQ_LANE_CAPACITY][PARAM_COUNT];
SEQ_STATE_D2 static uint16_t g_param_runtime_track_generation[SEQ_LANE_CAPACITY][PARAM_COUNT];
SEQ_STATE_D2 static uint8_t g_param_runtime_track_flags[SEQ_LANE_CAPACITY][PARAM_COUNT];
static uint16_t g_param_runtime_generation_counter;

static uint16_t param_registry_runtime_next_generation(void)
{
    ++g_param_runtime_generation_counter;
    if (g_param_runtime_generation_counter == 0U)
    {
        ++g_param_runtime_generation_counter;
    }
    return g_param_runtime_generation_counter;
}

void param_registry_runtime_state_init(void)
{
    memset(&g_param_runtime_track_values, 0, sizeof(g_param_runtime_track_values));
    memset(&g_param_runtime_track_generation, 0, sizeof(g_param_runtime_track_generation));
    memset(&g_param_runtime_track_flags, 0, sizeof(g_param_runtime_track_flags));
    g_param_runtime_generation_counter = 0U;
}

uint8_t param_registry_runtime_ui_value_get(uint8_t track,
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
    out_value->generation = g_param_runtime_track_generation[track][id];
    out_value->flags = flags;
    return 1U;
}

uint8_t param_registry_runtime_cache_get(uint8_t track, param_id_t id, float *out_value)
{
    param_registry_runtime_ui_value_t value;
    if ((out_value == NULL)
            || (param_registry_runtime_ui_value_get(track, id, &value) == 0U))
    {
        return 0U;
    }
    *out_value = value.base_value;
    return 1U;
}

void param_registry_runtime_cache_set(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT))
    {
        return;
    }

    const uint8_t was_valid = (uint8_t)(g_param_runtime_track_flags[track][id]
        & PARAM_REGISTRY_RUNTIME_UI_VALUE_VALID);
    if ((was_valid == 0U) || (g_param_runtime_track_values[track][id] != value))
    {
        g_param_runtime_track_values[track][id] = value;
        g_param_runtime_track_generation[track][id] =
            param_registry_runtime_next_generation();
    }
    g_param_runtime_track_flags[track][id] = PARAM_REGISTRY_RUNTIME_UI_VALUE_VALID;
}

void param_registry_runtime_cache_clear_track(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return;
    }

    memset(g_param_runtime_track_values[track], 0, sizeof(g_param_runtime_track_values[track]));
    memset(g_param_runtime_track_generation[track], 0, sizeof(g_param_runtime_track_generation[track]));
    memset(g_param_runtime_track_flags[track], 0, sizeof(g_param_runtime_track_flags[track]));
}

void param_registry_runtime_commit_authoritative_write(uint8_t track,
                                                       param_id_t id,
                                                       float value,
                                                       uint8_t resync_lfo)
{
    /* Post-commit helper: cache is authoritative, optional LFO resync stays explicit. */
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT))
    {
        return;
    }

    param_registry_runtime_cache_set(track, id, value);
    if (resync_lfo != 0U)
    {
        param_registry_runtime_resync_lfo(track, id, value);
    }
}

uint8_t param_registry_runtime_get_or_default(const param_desc_t *registry,
                                              param_id_t id,
                                              uint8_t track,
                                              float *out_value)
{
    /* Pure query helper: no cache write, no resync, only cache/default resolution. */
    if ((registry == NULL) || (id >= PARAM_COUNT) || (track >= SEQ_LANE_CAPACITY) || (out_value == NULL))
    {
        return 0U;
    }

    if (param_registry_runtime_cache_get(track, id, out_value) != 0U)
    {
        return 1U;
    }

    *out_value = registry[id].default_value;
    return 1U;
}

void param_registry_runtime_resync_lfo(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT))
    {
        return;
    }

    mod_lfo_v1_resync_base_on_authoritative_write(track, id, value);
}
