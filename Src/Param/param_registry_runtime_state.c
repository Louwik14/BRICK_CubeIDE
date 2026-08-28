#include "Param/param_registry_runtime_state.h"

#include "Storage/memory_layout.h"
#include "Seq/seq_types.h"
#include <stddef.h>

SEQ_STATE_D2 static float g_param_control_track_values[SEQ_LANE_CAPACITY][PARAM_COUNT];

void param_registry_control_values_init(void)
{
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        param_registry_control_values_reset_track(track);
}

uint8_t param_registry_control_value_get(uint8_t track, param_id_t id,
                                         float *out_value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT) || (out_value == NULL))
        return 0U;
    *out_value = g_param_control_track_values[track][id];
    return 1U;
}

void param_registry_control_value_set(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT))
        return;
    g_param_control_track_values[track][id] = value;
}

void param_registry_control_values_reset_track(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
        return;
    for (param_id_t id = 0U; id < PARAM_COUNT; ++id)
        g_param_control_track_values[track][id] = param_registry[id].default_value;
}
