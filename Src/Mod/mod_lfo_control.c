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
#include "Seq/seq_param_iface.h"
#include "Seq/seq_runtime.h"
#include "Track/entity_topology.h"
#include "Track/track_runtime.h"
#include "Track/track_sound_state.h"
#include "Track/tone_param_codec.h"
#include "Param/param_control_backends.h"

typedef struct
{
    float value[MOD_LFO_PARAM_COUNT];
} mod_lfo_control_state_t;

/* Canonical CONTROL authority for the three LFOs of each modulation owner. */
SEQ_STATE_D2 static mod_lfo_control_state_t
    g_mod_lfo_control_state[BRICK_ENTITY_CAPACITY][MOD_LFO_COUNT_PER_TRACK];

typedef struct
{
    uint32_t phase;
    uint32_t rng;
    float hold;
    uint8_t active;
    uint8_t hold_valid;
    uint8_t one_done;
} mod_lfo_control_midi_runtime_t;

static mod_lfo_control_midi_runtime_t
    g_mod_lfo_control_midi_runtime[BRICK_ENTITY_CAPACITY][MOD_LFO_COUNT_PER_TRACK];
static uint8_t g_mod_lfo_control_midi_active[SEQ_LANE_CAPACITY][12U];

static const float g_mod_lfo_control_sync_bars_per_cycle[MOD_LFO_SYNC_RATE_COUNT] = {
    8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.33333334f, 0.25f, 0.16666667f,
    0.125f, 0.08333334f, 0.0625f, 0.04166667f, 0.03125f,
    0.020833334f, 0.015625f, 0.0078125f
};

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
    memset(g_mod_lfo_control_midi_runtime, 0,
           sizeof(g_mod_lfo_control_midi_runtime));
    memset(g_mod_lfo_control_midi_active, 0,
           sizeof(g_mod_lfo_control_midi_active));
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
    if ((param == MOD_LFO_PARAM_SHAPE) || (param == MOD_LFO_PARAM_TRIG)
            || (param == MOD_LFO_PARAM_PHASE))
        memset(&g_mod_lfo_control_midi_runtime[owner][lfo_index], 0,
               sizeof(g_mod_lfo_control_midi_runtime[owner][lfo_index]));
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
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK
    };
    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        const float *const values = &canonical.lfo[lfo].rate;
        for (uint8_t param = 0U; param < MOD_LFO_PARAM_COUNT; ++param)
        {
            const param_id_t id = (param_id_t)(
                PARAM_LFO1_RATE + lfo * 4U + param);
            if (track_runtime_get_effective_param_status(owner, id)
                    != TRACK_RUNTIME_PARAM_ALLOWED) continue;
            bulk.item[bulk.count++] = (live_parameter_audio_bulk_item_t){
                .parameter_id = (uint16_t)id,
                .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
                .track = owner,
                .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
                .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                    | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
                .value = live_parameter_event_encode_float(
                    values[param])
            };
        }
    }
    if ((bulk.count != 0U)
            && !live_parameter_audio_publication_submit_bulk(&bulk)) return 0U;
    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        memcpy(g_mod_lfo_control_state[owner][lfo].value,
            &canonical.lfo[lfo].rate, sizeof(g_mod_lfo_control_state[owner][lfo].value));
    memset(g_mod_lfo_control_midi_runtime[owner], 0,
           sizeof(g_mod_lfo_control_midi_runtime[owner]));
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

static uint32_t mod_lfo_control_midi_phase_from_degrees(float degrees)
{
    if ((degrees <= 0.0f) || (degrees >= 360.0f)) return 0U;
    return (uint32_t)((double)degrees * (4294967296.0 / 360.0));
}

static uint32_t mod_lfo_control_midi_phase_delta(float rate,
                                                  uint32_t bpm_milli,
                                                  uint32_t frames)
{
    float hz = 0.0f;
    if (rate < -0.0001f)
        hz = -rate;
    else if (rate > 0.0001f)
    {
        uint8_t index = (uint8_t)(rate + 0.5f);
        if (index < 1U) index = 1U;
        if (index > MOD_LFO_SYNC_RATE_COUNT) index = MOD_LFO_SYNC_RATE_COUNT;
        float bpm = (float)bpm_milli * 0.001f;
        bpm = mod_lfo_control_clampf(bpm, 40.0f, 300.0f);
        const float seconds = g_mod_lfo_control_sync_bars_per_cycle[index - 1U]
            * (240.0f / bpm);
        hz = 1.0f / mod_lfo_control_clampf(seconds, 0.0005f, 60.0f);
    }
    if ((hz <= 0.0f) || (frames == 0U)) return 0U;
    const double delta = (double)hz * (double)frames
        * (4294967296.0 / 48000.0);
    return (delta >= 4294967295.0) ? 0xFFFFFFFFU
        : (uint32_t)(delta + 0.5);
}

static float mod_lfo_control_midi_random(mod_lfo_control_midi_runtime_t *rt)
{
    if (rt->rng == 0U) rt->rng = 0xA341316CU;
    rt->rng = rt->rng * 1664525U + 1013904223U;
    return ((float)((rt->rng >> 8) & 0x00FFFFFFU)
        * (2.0f / 16777215.0f)) - 1.0f;
}

static float mod_lfo_control_midi_source(uint8_t owner, uint8_t lfo,
                                         uint32_t frames, uint32_t bpm_milli,
                                         uint8_t *out_valid)
{
    const mod_lfo_control_state_t *const state =
        &g_mod_lfo_control_state[owner][lfo];
    mod_lfo_control_midi_runtime_t *const rt =
        &g_mod_lfo_control_midi_runtime[owner][lfo];
    const uint32_t delta = mod_lfo_control_midi_phase_delta(
        state->value[MOD_LFO_PARAM_RATE], bpm_milli, frames);
    if ((delta == 0U) || (rt->one_done != 0U))
    {
        *out_valid = 0U;
        return 0.0f;
    }
    const mod_lfo_trig_mode_t trig = (mod_lfo_trig_mode_t)(uint8_t)(
        state->value[MOD_LFO_PARAM_TRIG] + 0.5f);
    if (rt->active == 0U)
    {
        if (trig != MOD_LFO_TRIG_FREE)
        {
            *out_valid = 0U;
            return 0.0f;
        }
        rt->active = 1U;
        rt->phase = mod_lfo_control_midi_phase_from_degrees(
            state->value[MOD_LFO_PARAM_PHASE]);
    }
    const mod_lfo_shape_t shape = (mod_lfo_shape_t)(uint8_t)(
        state->value[MOD_LFO_PARAM_SHAPE] + 0.5f);
    float value;
    if (rt->hold_valid != 0U)
        value = rt->hold;
    else if (shape == MOD_LFO_SHAPE_RANDOM_SH)
    {
        if (rt->rng == 0U) rt->hold = mod_lfo_control_midi_random(rt);
        value = rt->hold;
    }
    else
        value = mod_lfo_segment_wave((uint8_t)shape, rt->phase, 0.0f);
    const uint32_t previous = rt->phase;
    rt->phase += delta;
    if (rt->phase < previous)
    {
        if (shape == MOD_LFO_SHAPE_RANDOM_SH)
            rt->hold = mod_lfo_control_midi_random(rt);
        if ((trig == MOD_LFO_TRIG_ONE) || (trig == MOD_LFO_TRIG_POLY_ONE))
            rt->one_done = 1U;
    }
    *out_valid = 1U;
    return value;
}

void mod_lfo_v1_control_note_trigger(uint8_t track)
{
    brick_entity_id_t owner = track;
    if ((entity_topology_mod_owner(track, &owner) == 0U)
            || (owner >= BRICK_ENTITY_CAPACITY)) return;
    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        const mod_lfo_control_state_t *const state =
            &g_mod_lfo_control_state[owner][lfo];
        const mod_lfo_trig_mode_t trig = (mod_lfo_trig_mode_t)(uint8_t)(
            state->value[MOD_LFO_PARAM_TRIG] + 0.5f);
        if (trig == MOD_LFO_TRIG_FREE) continue;
        mod_lfo_control_midi_runtime_t *const rt =
            &g_mod_lfo_control_midi_runtime[owner][lfo];
        rt->active = 1U;
        rt->one_done = 0U;
        rt->hold_valid = 0U;
        if ((trig == MOD_LFO_TRIG_HOLD) || (trig == MOD_LFO_TRIG_POLY_HOLD))
        {
            const mod_lfo_shape_t shape = (mod_lfo_shape_t)(uint8_t)(
                state->value[MOD_LFO_PARAM_SHAPE] + 0.5f);
            rt->hold = (shape == MOD_LFO_SHAPE_RANDOM_SH)
                ? mod_lfo_control_midi_random(rt)
                : mod_lfo_segment_wave((uint8_t)shape, rt->phase, 0.0f);
            rt->hold_valid = 1U;
        }
        else
            rt->phase = mod_lfo_control_midi_phase_from_degrees(
                state->value[MOD_LFO_PARAM_PHASE]);
    }
}

static uint8_t mod_lfo_control_midi_base(uint8_t track, param_id_t id,
                                         float *out_value)
{
    track_runtime_descriptor_t descriptor;
    uint8_t slot = 0U;
    seq_value16_t encoded = 0U;
    if ((out_value != NULL)
            && (track_runtime_get_descriptor(track, &descriptor) != 0U)
            && (tone_param_codec_param_to_slot(descriptor.type, id, &slot) != 0U)
            && (seq_param_iface_get_runtime_value(track, SEQ_PLOCK_SET_TONE,
                                                  slot, &encoded) != 0U))
        return seq_param_iface_decode_param_value(id, encoded, out_value);
    return param_registry_get_track_value(id, track, out_value);
}

void mod_lfo_v1_control_process(uint32_t frames)
{
    float source[BRICK_ENTITY_CAPACITY][MOD_LFO_COUNT_PER_TRACK] = {{0.0f}};
    uint8_t source_valid[BRICK_ENTITY_CAPACITY][MOD_LFO_COUNT_PER_TRACK] = {{0U}};
    float sum[SEQ_LANE_CAPACITY][12U] = {{0.0f}};
    uint8_t active[SEQ_LANE_CAPACITY][12U] = {{0U}};
    const uint32_t bpm_milli = seq_runtime_get_effective_tempo_bpm_milli();
    for (uint8_t owner = 0U; owner < BRICK_ENTITY_CAPACITY; ++owner)
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
            source[owner][lfo] = mod_lfo_control_midi_source(
                owner, lfo, frames, bpm_milli, &source_valid[owner][lfo]);

    for (uint8_t owner = 0U; owner < SEQ_LANE_CAPACITY; ++owner)
    {
        const track_sound_state_t *const matrix = track_sound_state_get_const(owner);
        if (matrix == NULL) continue;
        for (uint8_t route = 0U; route < MOD_MATRIX_SLOT_COUNT; ++route)
        {
            const track_mod_matrix_slot_t *const item = &matrix->mod_matrix[route];
            uint8_t target = 0U;
            param_id_t id = PARAM_COUNT;
            if ((item->enabled == 0U) || (item->depth == 0.0f)
                    || (item->source < (uint8_t)MOD_MATRIX_SOURCE_LFO1)
                    || (item->source > (uint8_t)MOD_MATRIX_SOURCE_LFO3)
                    || (mod_destination_address_resolve(
                        item->destination, &target, &id) == 0U)
                    || (target >= SEQ_LANE_CAPACITY)
                    || (param_backend_is_midi_cc_id(id) == 0U))
                continue;
            const uint8_t lfo = (uint8_t)(item->source
                - (uint8_t)MOD_MATRIX_SOURCE_LFO1);
            if (source_valid[owner][lfo] == 0U) continue;
            const uint8_t cc = (uint8_t)(id - PARAM_MIDI_CC1_1);
            sum[target][cc] += source[owner][lfo] * item->depth;
            active[target][cc] = 1U;
        }
    }
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        for (uint8_t cc = 0U; cc < 12U; ++cc)
        {
            if ((active[track][cc] == 0U)
                    && (g_mod_lfo_control_midi_active[track][cc] == 0U))
                continue;
            const param_id_t id = (param_id_t)(PARAM_MIDI_CC1_1 + cc);
            float base = 0.0f;
            if (mod_lfo_control_midi_base(track, id, &base) != 0U)
                (void)param_backend_send_midi_cc(track, id,
                    mod_lfo_control_clampf(base + sum[track][cc], 0.0f, 127.0f));
            g_mod_lfo_control_midi_active[track][cc] = active[track][cc];
        }
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
