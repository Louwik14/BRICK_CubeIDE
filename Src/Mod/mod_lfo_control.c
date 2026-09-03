#include "Mod/mod_lfo_v1_control.h"
#include "Track/entity_types.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "Mod/mod_destination_control.h"
#include "Mod/mod_lfo_segment.h"
#include "IPC/live_clock_control.h"
#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"
#include "Param/param_registry.h"
#include "Platform/memory_layout.h"
#include "Seq/seq_types.h"
#include "Track/entity_topology.h"

typedef struct
{
    float value[MOD_LFO_PARAM_COUNT];
} mod_lfo_control_state_t;

/* Canonical CONTROL authority for the three LFOs of each modulation owner. */
SEQ_STATE_D2 static mod_lfo_control_state_t
    g_mod_lfo_control_state[BRICK_ENTITY_CAPACITY][MOD_LFO_COUNT_PER_TRACK];

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

static uint8_t mod_lfo_control_canonicalize(mod_lfo_param_t param,
                                             float value,
                                             float *out_value)
{
    if ((out_value == NULL) || !isfinite(value)) return 0U;
    switch (param)
    {
        case MOD_LFO_PARAM_RATE:
            value = mod_lfo_control_clampf(
                value, -LFO_FREE_MAX_HZ, (float)MOD_LFO_SYNC_RATE_COUNT);
            if (value > 0.0f) value = mod_lfo_control_quantize_sync_rate(value);
            break;
        case MOD_LFO_PARAM_SHAPE:
            value = mod_lfo_control_clampf(
                value, 0.0f, (float)((uint8_t)MOD_LFO_SHAPE_COUNT - 1U));
            value = (float)(uint8_t)(value + 0.5f);
            break;
        case MOD_LFO_PARAM_TRIG:
            value = mod_lfo_control_clampf(
                value, 0.0f, (float)((uint8_t)MOD_LFO_TRIG_COUNT - 1U));
            value = (float)(uint8_t)(value + 0.5f);
            break;
        case MOD_LFO_PARAM_PHASE:
            value = mod_lfo_control_clampf(value, 0.0f, 360.0f);
            break;
        default: return 0U;
    }
    *out_value = value;
    return 1U;
}

uint8_t mod_lfo_v1_prepare_bank(const mod_lfo_control_bank_t *state,
                                mod_lfo_control_bank_t *out)
{
    if ((state == NULL) || (out == NULL)) return 0U;
    for (uint8_t lfo=0U;lfo<MOD_LFO_COUNT_PER_TRACK;++lfo)
    {
        const float in[MOD_LFO_PARAM_COUNT]={state->lfo[lfo].rate,
            state->lfo[lfo].shape,state->lfo[lfo].trigger,state->lfo[lfo].phase};
        float *const value=&out->lfo[lfo].rate;
        for(uint8_t p=0U;p<MOD_LFO_PARAM_COUNT;++p)
            if(!mod_lfo_control_canonicalize((mod_lfo_param_t)p,in[p],&value[p]))return 0U;
    }
    return 1U;
}

void mod_lfo_v1_init(void)
{
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            for (uint8_t param = 0U; param < (uint8_t)MOD_LFO_PARAM_COUNT; ++param)
            {
                const param_id_t id = (param_id_t)(PARAM_LFO1_RATE
                    + lfo * 4U + param);
                g_mod_lfo_control_state[track][lfo].value[param] =
                    param_registry[id].default_value;
            }
        }
    }
    mod_destination_catalog_init();
    mod_lfo_v1_invalidate_dest_cache_all();
}

uint8_t mod_lfo_v1_reset_track(uint8_t track)
{
    mod_lfo_control_bank_t state;
    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        state.lfo[lfo] = (mod_lfo_control_value_t){
            .rate = param_registry[PARAM_LFO1_RATE + lfo * 4U].default_value,
            .shape = param_registry[PARAM_LFO1_SHAPE + lfo * 4U].default_value,
            .trigger = param_registry[PARAM_LFO1_TRIG + lfo * 4U].default_value,
            .phase = param_registry[PARAM_LFO1_PHASE + lfo * 4U].default_value
        };
    }
    return mod_lfo_v1_restore_track(track, &state);
}

uint8_t mod_lfo_v1_prepare_track_param(uint8_t track, uint8_t lfo_index,
                                       mod_lfo_param_t param, float value,
                                       uint8_t *out_owner,
                                       float *out_canonical_value)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK)
            || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT)
            || (out_owner == NULL) || (out_canonical_value == NULL))
        return 0U;

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    if (mod_lfo_control_canonicalize(
            param, value, out_canonical_value) == 0U) return 0U;
    *out_owner = owner;
    return 1U;
}

uint8_t mod_lfo_v1_install_prepared_track_param(uint8_t owner,
                                                uint8_t lfo_index,
                                                mod_lfo_param_t param,
                                                float canonical_value)
{
    if ((owner >= BRICK_ENTITY_CAPACITY)
            || (lfo_index >= MOD_LFO_COUNT_PER_TRACK)
            || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT)
            || !isfinite(canonical_value)) return 0U;
    g_mod_lfo_control_state[owner][lfo_index].value[(uint8_t)param] =
        canonical_value;
    return 1U;
}

uint8_t mod_lfo_v1_restore_track(uint8_t track,
                                 const mod_lfo_control_bank_t *state)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (state == NULL)) return 0U;
    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    mod_lfo_control_bank_t canonical;
    if (mod_lfo_v1_prepare_bank(state, &canonical) == 0U) return 0U;
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
    };
    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        const float *const values = &canonical.lfo[lfo].rate;
        for (uint8_t param = 0U; param < MOD_LFO_PARAM_COUNT; ++param)
        {
            bulk.item[bulk.count++] = (live_parameter_audio_bulk_item_t){
                .parameter_id = (uint16_t)(PARAM_LFO1_RATE + lfo * 4U + param),
                .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
                .track = owner,
                .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
                .flags = LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
                .value = live_parameter_event_encode_float(
                    values[param])
            };
        }
    }
    if (!live_parameter_audio_publication_submit_bulk(&bulk)) return 0U;
    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        memcpy(g_mod_lfo_control_state[owner][lfo].value,
            &canonical.lfo[lfo].rate, sizeof(g_mod_lfo_control_state[owner][lfo].value));
    return 1U;
}

uint8_t mod_lfo_v1_get_track_param(uint8_t track, uint8_t lfo_index,
                                   mod_lfo_param_t param, float *out_value)
{
    if ((track >= BRICK_ENTITY_CAPACITY) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK)
            || (out_value == NULL)
            || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
        return 0U;

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    *out_value = g_mod_lfo_control_state[owner][lfo_index].value[(uint8_t)param];
    return 1U;
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
