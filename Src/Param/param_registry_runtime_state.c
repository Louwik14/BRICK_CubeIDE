#include "Param/param_registry_runtime_state.h"

#include "Platform/memory_layout.h"
#include "Seq/seq_types.h"
#include "Track/track_runtime.h"
#include <stddef.h>

#define PARAM_CONTROL_NON_TONE_CAPACITY 256U
#define PARAM_CONTROL_STORAGE_INVALID UINT16_MAX

SEQ_STATE_D2 static float
    g_param_control_track_values[SEQ_LANE_CAPACITY][PARAM_CONTROL_NON_TONE_CAPACITY];
SEQ_STATE_D2 static float
    g_param_control_tone[SEQ_LANE_CAPACITY][SEQ_PARAM_TONE_SLOT_COUNT];
static uint16_t g_param_control_storage_index[PARAM_COUNT];

static float tone_normalize(param_id_t id, float value)
{
    const float span = param_registry[id].max - param_registry[id].min;
    if (span <= 0.0f) return 0.0f;
    value = (value - param_registry[id].min) / span;
    return (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
}

static float tone_expand(param_id_t id, float normalized)
{
    normalized = (normalized < 0.0f) ? 0.0f
        : ((normalized > 1.0f) ? 1.0f : normalized);
    return param_registry[id].min
        + normalized * (param_registry[id].max - param_registry[id].min);
}

static uint8_t tone_slot_for_current_track(uint8_t track, param_id_t id,
                                           uint8_t *out_slot)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    return (ctx != NULL)
        ? track_runtime_tone_param_to_slot(
            (track_runtime_type_t)ctx->type, id, out_slot)
        : 0U;
}

void param_registry_control_values_init(void)
{
    uint16_t storage_count = 0U;
    for (param_id_t id = 0U; id < PARAM_COUNT; ++id)
    {
        g_param_control_storage_index[id] = PARAM_CONTROL_STORAGE_INVALID;
        if ((param_id_is_reserved(id) != 0U)
                || (track_runtime_get_param_rule(id).domain
                    == TRACK_RUNTIME_PARAM_DOMAIN_TONE))
            continue;
        if (storage_count < PARAM_CONTROL_NON_TONE_CAPACITY)
            g_param_control_storage_index[id] = storage_count++;
    }
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        param_registry_control_values_reset_track(track);
}

uint8_t param_registry_control_value_get(uint8_t track, param_id_t id,
                                         float *out_value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT) || (out_value == NULL))
        return 0U;
    uint8_t tone_slot = 0U;
    if (tone_slot_for_current_track(track, id, &tone_slot) != 0U)
    {
        *out_value = tone_expand(id, g_param_control_tone[track][tone_slot]);
        return 1U;
    }
    const uint16_t storage = g_param_control_storage_index[id];
    if (storage == PARAM_CONTROL_STORAGE_INVALID) return 0U;
    *out_value = g_param_control_track_values[track][storage];
    return 1U;
}

void param_registry_control_value_set(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT))
        return;
    uint8_t tone_slot = 0U;
    if (tone_slot_for_current_track(track, id, &tone_slot) != 0U)
    {
        g_param_control_tone[track][tone_slot] = tone_normalize(id, value);
        return;
    }
    const uint16_t storage = g_param_control_storage_index[id];
    if (storage != PARAM_CONTROL_STORAGE_INVALID)
        g_param_control_track_values[track][storage] = value;
}

void param_registry_control_values_reset_track(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
        return;
    for (param_id_t id = 0U; id < PARAM_COUNT; ++id)
    {
        const uint16_t storage = g_param_control_storage_index[id];
        if (storage != PARAM_CONTROL_STORAGE_INVALID)
            g_param_control_track_values[track][storage] =
                param_registry[id].default_value;
    }
    for (uint8_t slot = 0U; slot < SEQ_PARAM_TONE_SLOT_COUNT; ++slot)
        g_param_control_tone[track][slot] = 0.0f;
}

uint8_t param_registry_control_tone_get(uint8_t track, uint8_t slot,
                                        float *out_normalized)
{
    if ((track >= SEQ_LANE_CAPACITY) || (slot >= SEQ_PARAM_TONE_SLOT_COUNT)
            || (out_normalized == NULL)) return 0U;
    *out_normalized = g_param_control_tone[track][slot];
    return 1U;
}

uint8_t param_registry_control_tone_set(uint8_t track, uint8_t slot,
                                        float normalized)
{
    if ((track >= SEQ_LANE_CAPACITY) || (slot >= SEQ_PARAM_TONE_SLOT_COUNT))
        return 0U;
    g_param_control_tone[track][slot] =
        (normalized < 0.0f) ? 0.0f
        : ((normalized > 1.0f) ? 1.0f : normalized);
    return 1U;
}
