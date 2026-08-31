#include "Mod/mod_lfo_v1.h"

#include <stddef.h>

#include "Mod/mod_destination_control.h"
#include "Mod/mod_lfo_segment.h"
#include "Param/param_registry_runtime_state.h"
#include "Track/entity_topology.h"

static float mod_lfo_control_clampf(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float mod_lfo_control_quantize_sync_rate(float rate)
{
    if (rate <= 0.0001f) return rate;
    uint8_t sync = (uint8_t)(rate + 0.5f);
    if (sync == 0U) sync = 1U;
    if (sync > MOD_LFO_SYNC_RATE_COUNT) sync = MOD_LFO_SYNC_RATE_COUNT;
    return (float)sync;
}

void mod_lfo_v1_init(void)
{
    mod_destination_catalog_init();
    mod_lfo_v1_invalidate_dest_cache_all();
}

uint8_t mod_lfo_v1_set_track_param(uint8_t track, uint8_t lfo_index,
                                   mod_lfo_param_t param, float value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK)
            || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
        return 0U;

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    track = owner;
    const param_id_t id = (param_id_t)(PARAM_LFO1_RATE
        + lfo_index * 4U + (uint8_t)param);
    float canonical = value;
    switch (param)
    {
        case MOD_LFO_PARAM_RATE:
            canonical = mod_lfo_control_clampf(
                value, -LFO_FREE_MAX_HZ, (float)MOD_LFO_SYNC_RATE_COUNT);
            if (canonical > 0.0f)
                canonical = mod_lfo_control_quantize_sync_rate(canonical);
            break;
        case MOD_LFO_PARAM_SHAPE:
            canonical = mod_lfo_control_clampf(
                value, 0.0f, (float)((uint8_t)MOD_LFO_SHAPE_COUNT - 1U));
            break;
        case MOD_LFO_PARAM_TRIG:
            canonical = mod_lfo_control_clampf(
                value, 0.0f, (float)((uint8_t)MOD_LFO_TRIG_COUNT - 1U));
            break;
        case MOD_LFO_PARAM_PHASE:
            canonical = mod_lfo_control_clampf(value, 0.0f, 360.0f);
            break;
        default:
            return 0U;
    }
    param_registry_control_value_set(track, id, canonical);
    return 1U;
}

uint8_t mod_lfo_v1_get_track_param(uint8_t track, uint8_t lfo_index,
                                   mod_lfo_param_t param, float *out_value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK)
            || (out_value == NULL)
            || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
        return 0U;

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    return param_registry_control_value_get(
        owner,
        (param_id_t)(PARAM_LFO1_RATE + lfo_index * 4U + (uint8_t)param),
        out_value);
}

uint8_t mod_lfo_v1_shape_is_random(uint8_t track, uint8_t lfo_index)
{
    float shape = 0.0f;
    if (mod_lfo_v1_get_track_param(track, lfo_index,
                                   MOD_LFO_PARAM_SHAPE, &shape) == 0U)
        return 0U;
    return ((uint8_t)(shape + 0.5f) == (uint8_t)MOD_LFO_SHAPE_RANDOM_SH) ? 1U : 0U;
}

uint8_t mod_lfo_v1_waveform_point(uint8_t track, uint8_t lfo_index,
                                  uint8_t x, uint8_t width, int8_t *out_y_q7)
{
    if ((out_y_q7 == NULL) || (width == 0U)) return 0U;
    float shape_value = 0.0f;
    if (mod_lfo_v1_get_track_param(track, lfo_index,
                                   MOD_LFO_PARAM_SHAPE, &shape_value) == 0U)
        return 0U;
    const mod_lfo_shape_t shape =
        (mod_lfo_shape_t)((uint8_t)(shape_value + 0.5f));
    if (shape == MOD_LFO_SHAPE_RANDOM_SH)
    {
        static const int8_t pattern[8] = {-48, 32, 84, -16, -80, 4, 56, -28};
        *out_y_q7 = pattern[(width > 1U) ? (((uint16_t)x * 8U) / width) & 7U : 0U];
        return 1U;
    }

    const uint32_t phase = (uint32_t)(((uint64_t)x * 4294967296ULL) / width);
    float value = mod_lfo_segment_wave((uint8_t)shape, phase, 0.0f);
    if ((shape == MOD_LFO_SHAPE_SINE_POS)
            || (shape == MOD_LFO_SHAPE_TRIANGLE_POS)
            || (shape == MOD_LFO_SHAPE_SQUARE_POS))
        value = (value * 2.0f) - 1.0f;
    value = mod_lfo_control_clampf(value, -1.0f, 1.0f);
    *out_y_q7 = (int8_t)(value * 63.0f);
    return 1U;
}

uint16_t mod_lfo_v1_dest_count(uint8_t track)
{
    return mod_destination_catalog_count(track);
}

uint8_t mod_lfo_v1_dest_param_at(uint8_t track, uint16_t dest_index,
                                 param_id_t *out_param)
{
    if (out_param == NULL) return 0U;
    *out_param = mod_destination_catalog_param_from_index(track, dest_index);
    return 1U;
}

void mod_lfo_v1_invalidate_dest_cache_track(uint8_t track)
{
    mod_destination_catalog_invalidate_track(track);
}

void mod_lfo_v1_invalidate_dest_cache_all(void)
{
    mod_destination_catalog_invalidate_all();
}

uint8_t mod_lfo_v1_dest_label(uint8_t track, uint16_t dest_index,
                              char *out, uint32_t out_len)
{
    return mod_destination_catalog_label(track, dest_index, out, out_len);
}

uint8_t mod_lfo_v1_dest_short_label(uint8_t track, uint16_t dest_index,
                                    char *out, uint32_t out_len)
{
    return mod_destination_catalog_short_label(track, dest_index, out, out_len);
}
