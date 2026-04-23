#include "Param/param_registry_runtime_state.h"

#include "Mod/mod_lfo_v1.h"
#include "Storage/memory_layout.h"
#include <string.h>

SEQ_STATE_D2 static float g_param_runtime_track_values[SEQ_TRACK_COUNT][PARAM_COUNT];
SEQ_STATE_D2 static uint8_t g_param_runtime_track_valid[SEQ_TRACK_COUNT][PARAM_COUNT];

void param_registry_runtime_state_init(void)
{
    memset(&g_param_runtime_track_values, 0, sizeof(g_param_runtime_track_values));
    memset(&g_param_runtime_track_valid, 0, sizeof(g_param_runtime_track_valid));
}

uint8_t param_registry_runtime_cache_get(uint8_t track, param_id_t id, float *out_value)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT) || (out_value == NULL))
    {
        return 0U;
    }

    if (g_param_runtime_track_valid[track][id] == 0U)
    {
        return 0U;
    }

    *out_value = g_param_runtime_track_values[track][id];
    return 1U;
}

void param_registry_runtime_cache_set(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    g_param_runtime_track_values[track][id] = value;
    g_param_runtime_track_valid[track][id] = 1U;
}

void param_registry_runtime_commit_authoritative_write(uint8_t track,
                                                       param_id_t id,
                                                       float value,
                                                       uint8_t resync_lfo)
{
    /* Post-commit helper: cache is authoritative, optional LFO resync stays explicit. */
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
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
    if ((registry == NULL) || (id >= PARAM_COUNT) || (track >= SEQ_TRACK_COUNT) || (out_value == NULL))
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
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    mod_lfo_v1_resync_base_on_authoritative_write(track, id, value);
}
