#include "Mod/mod_lfo_v1.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Mod/mod_lfo_segment.h"
#include "Storage/memory_layout.h"

#include <string.h>

#include "Core/brick6_audio_event_grid.h"
#include "Core/entity_topology.h"
#include "Core/synth_polyphony.h"
#include "Core/track_sound_state.h"
#include "Core/track_runtime.h"
#include "Audio/mixer.h"
#include "Mod/mod_destination_catalog.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_matrix.h"
#include "Param/param_registry.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "ui_core.h"

/* GROUP modulation state is owned by its master entity. */
#undef SEQ_TRACK_COUNT
#define SEQ_TRACK_COUNT SEQ_LANE_CAPACITY

#define MOD_LFO_AUDIO_SAMPLE_RATE 48000.0f
#define MOD_LFO_CONTROL_RATE_HZ 3000.0f
#define MOD_LFO_LEGACY_CONTROL_STRIDE ((uint32_t)(MOD_LFO_AUDIO_SAMPLE_RATE / MOD_LFO_CONTROL_RATE_HZ))
#define MOD_LFO_WINDOW_RATE_EXPERIMENT 1U
#ifndef MOD_LFO_WINDOW_RATE_FRAMES
#define MOD_LFO_WINDOW_RATE_FRAMES BRICK6_AUDIO_EVENT_GRID_FRAMES
#endif
#if MOD_LFO_WINDOW_RATE_EXPERIMENT
#define MOD_LFO_CONTROL_STRIDE ((uint32_t)MOD_LFO_WINDOW_RATE_FRAMES)
#define MOD_LFO_PHASE_DT (1.0f / MOD_LFO_AUDIO_SAMPLE_RATE)
#else
#define MOD_LFO_CONTROL_STRIDE MOD_LFO_LEGACY_CONTROL_STRIDE
#define MOD_LFO_PHASE_DT (1.0f / MOD_LFO_CONTROL_RATE_HZ)
#endif
#define MOD_LFO_RATE_OFF_EPS 0.0001f

/* Positive RATE values index this tempo-sync table: 1=8BAR ... 16=1/128. */
static const float g_mod_lfo_sync_bars_per_cycle[MOD_LFO_SYNC_RATE_COUNT] = {
    8.0f, 4.0f, 2.0f, 1.0f,
    0.5f, 0.33333334f,
    0.25f, 0.16666667f,
    0.125f, 0.08333334f,
    0.0625f, 0.04166667f,
    0.03125f, 0.020833334f,
    0.015625f, 0.0078125f
};

typedef struct
{
    uint32_t phase;
    uint32_t phase_inc;
    float current;
    float hold_value;
    float slew_value;
    uint32_t rng_state;
    float sh_value;
    uint8_t sh_valid;
    uint8_t triggered;
    uint8_t one_running;
    uint8_t one_done;
    uint8_t hold_valid;
    uint8_t slew_valid;
    uint8_t active;
    mod_lfo_ramp_t ramp;
    uint8_t ramp_valid;
    float ramp_end;
    uint8_t ramp_discontinuous;
    uint8_t temp_valid_mask;
    track_mod_lfo_state_t temp;
} mod_lfo_runtime_state_t;

static mod_lfo_runtime_state_t g_mod_lfo_runtime[SEQ_TRACK_COUNT][MOD_LFO_COUNT_PER_TRACK];
typedef struct
{
    uint32_t phase;
    uint32_t rng_state;
    float sh_value;
    float hold_value;
    float slew_value;
    uint8_t sh_valid;
    uint8_t hold_valid;
    uint8_t slew_valid;
    uint8_t one_done;
    uint8_t active;
    uint8_t pending_trigger;
    uint8_t pending_reset;
} mod_lfo_poly_state_t;

static mod_lfo_poly_state_t
    g_mod_lfo_poly_runtime[MOD_LFO_POLY_SLOT_COUNT][MOD_LFO_COUNT_PER_TRACK];
static uint8_t g_mod_lfo_poly_owner[MOD_LFO_POLY_SLOT_COUNT];

typedef struct
{
    uint32_t phase_inc;
    uint32_t frames;
    float random_slew_coeff;
    uint8_t shape;
    uint8_t flags;
    uint8_t valid;
} mod_lfo_poly_segment_config_t;

#define MOD_LFO_POLY_FLAG_RANDOM       (1U << 0)
#define MOD_LFO_POLY_FLAG_HOLD         (1U << 1)
#define MOD_LFO_POLY_FLAG_ONE          (1U << 2)
#define MOD_LFO_POLY_FLAG_SPLIT_SHIFT  3U
#define MOD_LFO_POLY_FLAG_SPLIT_MASK   (0x07U << MOD_LFO_POLY_FLAG_SPLIT_SHIFT)

static mod_lfo_poly_segment_config_t
    g_mod_lfo_poly_segment_config[SEQ_TRACK_COUNT][MOD_LFO_COUNT_PER_TRACK];
#define MOD_LFO_POLY_PREPARED_ENTRY_CAPACITY \
    (SEQ_TRACK_COUNT * MOD_LFO_COUNT_PER_TRACK)
static uint8_t g_mod_lfo_poly_prepared_entries[MOD_LFO_POLY_PREPARED_ENTRY_CAPACITY];
static uint8_t g_mod_lfo_poly_prepared_entry_count = 0U;

_Static_assert(SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET == MOD_LFO_POLY_SYNTH_SLOT_COUNT,
               "Poly LFO synth slot namespace changed");
static uint32_t g_mod_lfo_control_counter = 0U;
static uint8_t g_mod_lfo_had_matrix_routes = 0U;
static uint8_t g_mod_lfo_track_had_matrix_routes[SEQ_TRACK_COUNT];

static uint8_t mod_lfo_trig_is_poly(mod_lfo_trig_mode_t trig)
{
    return (trig >= MOD_LFO_TRIG_POLY_TRIG) ? 1U : 0U;
}

static void mod_lfo_poly_state_invalidate(mod_lfo_poly_state_t *rt,
                                          uint8_t wait_for_trigger)
{
    if (rt == NULL)
    {
        return;
    }

    rt->phase = 0U;
    rt->rng_state = 0U;
    rt->sh_value = 0.0f;
    rt->hold_value = 0.0f;
    rt->slew_value = 0.0f;
    rt->sh_valid = 0U;
    rt->hold_valid = 0U;
    rt->slew_valid = 0U;
    rt->one_done = 0U;
    rt->active = 0U;
    rt->pending_trigger = wait_for_trigger;
    rt->pending_reset = wait_for_trigger;
}

static void mod_lfo_poly_mode_changed(uint8_t track,
                                      uint8_t lfo_index,
                                      mod_lfo_trig_mode_t old_trig,
                                      mod_lfo_trig_mode_t new_trig)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK)
            || (old_trig == new_trig))
    {
        return;
    }

    if ((mod_lfo_trig_is_poly(old_trig) == 0U)
            && (mod_lfo_trig_is_poly(new_trig) == 0U))
    {
        return;
    }

    for (uint8_t voice_slot = 0U; voice_slot < MOD_LFO_POLY_SLOT_COUNT; ++voice_slot)
    {
        if (g_mod_lfo_poly_owner[voice_slot] == track)
        {
            mod_lfo_poly_state_invalidate(&g_mod_lfo_poly_runtime[voice_slot][lfo_index],
                                          mod_lfo_trig_is_poly(new_trig));
        }
    }
}

static float mod_lfo_clampf(float v, float lo, float hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

static track_mod_lfo_state_t *mod_lfo_track_settings_mut(uint8_t track, uint8_t lfo_index)
{
    track_sound_state_t *const state = track_sound_state_get(track);

    if ((state == NULL) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK))
    {
        return NULL;
    }

    return &state->mod_lfo[lfo_index];
}

static const track_mod_lfo_state_t *mod_lfo_track_settings_const(uint8_t track, uint8_t lfo_index)
{
    const track_sound_state_t *const state = track_sound_state_get_const(track);

    if ((state == NULL) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK))
    {
        return NULL;
    }

    return &state->mod_lfo[lfo_index];
}

static ui_track_family_t mod_lfo_ui_family_from_ctx(const track_audio_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return UI_TRACK_FAMILY_OFF;
    }

    switch ((track_runtime_family_t)ctx->family)
    {
        case TRACK_RUNTIME_FAMILY_SYNTH:
            return UI_TRACK_FAMILY_SYNTH;
        case TRACK_RUNTIME_FAMILY_SAMPLER:
            return UI_TRACK_FAMILY_SAMPLER;
        case TRACK_RUNTIME_FAMILY_DRUM:
            return UI_TRACK_FAMILY_DRUM;
            return UI_TRACK_FAMILY_OFF;
        case TRACK_RUNTIME_FAMILY_MIDI:
            return UI_TRACK_FAMILY_MIDI;
        case TRACK_RUNTIME_FAMILY_EXTERNAL:
            return UI_TRACK_FAMILY_EXTERNAL;
        case TRACK_RUNTIME_FAMILY_OFF:
        default:
            return UI_TRACK_FAMILY_OFF;
    }
}

static ui_track_type_t mod_lfo_ui_type_from_ctx(const track_audio_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return UI_TRACK_TYPE_NONE;
    }

    switch ((track_runtime_type_t)ctx->type)
    {
        case TRACK_RUNTIME_TYPE_RAM:
            return UI_TRACK_TYPE_RAM;
        case TRACK_RUNTIME_TYPE_PRISM:
            return UI_TRACK_TYPE_PRISM;
        case TRACK_RUNTIME_TYPE_WAVE:
            return UI_TRACK_TYPE_WAVE;
        case TRACK_RUNTIME_TYPE_STACK:
            return UI_TRACK_TYPE_STACK;
        case TRACK_RUNTIME_TYPE_DRUM_MD:
            return UI_TRACK_TYPE_DRUM_MD;
        case TRACK_RUNTIME_TYPE_MIDI:
            return UI_TRACK_TYPE_MIDI;
        case TRACK_RUNTIME_TYPE_EXTERNAL:
            return UI_TRACK_TYPE_EXTERNAL;
        case TRACK_RUNTIME_TYPE_STREAM:
            return UI_TRACK_TYPE_STREAM;
        case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:
            return UI_TRACK_TYPE_DRUM_BD_ANALOG;
        case TRACK_RUNTIME_TYPE_LOOPER:
            return UI_TRACK_TYPE_LOOPER;
        case TRACK_RUNTIME_TYPE_MULTI:
            return UI_TRACK_TYPE_MULTI;
        case TRACK_RUNTIME_TYPE_NONE:
        default:
            return UI_TRACK_TYPE_NONE;
    }
}

static uint8_t mod_lfo_runtime_param_mask(mod_lfo_param_t param)
{
    return (uint8_t)(1U << (uint8_t)param);
}

static float mod_lfo_effective_field(const mod_lfo_runtime_state_t *rt,
                                     const track_mod_lfo_state_t *s,
                                     mod_lfo_param_t param)
{
    const uint8_t mask = mod_lfo_runtime_param_mask(param);
    const track_mod_lfo_state_t *const source =
        ((rt != NULL) && ((rt->temp_valid_mask & mask) != 0U)) ? &rt->temp : s;

    if (source == NULL)
    {
        return 0.0f;
    }

    switch (param)
    {
        case MOD_LFO_PARAM_RATE:
            return source->rate;
        case MOD_LFO_PARAM_SHAPE:
            return source->shape;
        case MOD_LFO_PARAM_TRIG:
            return source->trig;
        case MOD_LFO_PARAM_PHASE:
            return source->phase;
        default:
            return 0.0f;
    }
}

static uint32_t mod_lfo_phase_inc_from_hz(float hz)
{
    hz = mod_lfo_clampf(hz, 0.0f, 12000.0f);
    if (hz <= MOD_LFO_RATE_OFF_EPS)
    {
        return 0U;
    }
    const double phase_f = (double)hz * (4294967296.0 * (double)MOD_LFO_PHASE_DT);
    if (phase_f <= 1.0)
    {
        return 1U;
    }
    if (phase_f >= 4294967295.0)
    {
        return 0xFFFFFFFFU;
    }
    return (uint32_t)(phase_f + 0.5);
}

static float mod_lfo_quantize_sync_rate(float rate)
{
    if (rate <= MOD_LFO_RATE_OFF_EPS)
    {
        return rate;
    }

    uint8_t sync = (uint8_t)(rate + 0.5f);
    if (sync == 0U)
    {
        sync = 1U;
    }
    if (sync > MOD_LFO_SYNC_RATE_COUNT)
    {
        sync = MOD_LFO_SYNC_RATE_COUNT;
    }
    return (float)sync;
}

static uint32_t mod_lfo_phase_inc_from_rate_with_bpm(float rate, uint32_t bpm_milli)
{
    if (rate < -MOD_LFO_RATE_OFF_EPS)
    {
        return mod_lfo_phase_inc_from_hz(-rate);
    }
    if (rate > MOD_LFO_RATE_OFF_EPS)
    {
        uint8_t idx = (uint8_t)mod_lfo_quantize_sync_rate(rate);
        idx--;
        if (idx >= MOD_LFO_SYNC_RATE_COUNT)
        {
            idx = MOD_LFO_SYNC_RATE_COUNT - 1U;
        }
        const float bpm = (float)bpm_milli * 0.001f;
        const float bars_per_cycle = g_mod_lfo_sync_bars_per_cycle[idx];
        const float seconds_per_cycle = bars_per_cycle * (240.0f / mod_lfo_clampf(bpm, 40.0f, 300.0f));
        return mod_lfo_phase_inc_from_hz(1.0f / mod_lfo_clampf(seconds_per_cycle, 0.0005f, 60.0f));
    }
    return 0U;
}

static uint32_t mod_lfo_phase_inc_from_rate(float rate)
{
    return mod_lfo_phase_inc_from_rate_with_bpm(rate, seq_runtime_get_tempo_bpm_milli());
}

static uint32_t mod_lfo_phase_from_degrees(float degrees)
{
    degrees = mod_lfo_clampf(degrees, 0.0f, 360.0f);
    if (degrees >= 360.0f)
    {
        return 0U;
    }
    return (uint32_t)(((double)degrees / 360.0) * 4294967296.0);
}

static void mod_lfo_start_phase(mod_lfo_runtime_state_t *rt, mod_lfo_shape_t shape, float phase)
{
    if (rt == NULL)
    {
        return;
    }

    if (shape == MOD_LFO_SHAPE_RANDOM_SH)
    {
        rt->phase = 0U;
        rt->sh_valid = 0U;
    }
    else
    {
        rt->phase = mod_lfo_phase_from_degrees(phase);
    }
    rt->slew_valid = 0U;
}

static uint32_t mod_lfo_xorshift32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static float mod_lfo_sh_next_value(uint32_t *state)
{
    uint32_t s = mod_lfo_xorshift32(*state);
    const uint32_t a = s;
    s = mod_lfo_xorshift32(s);
    const uint32_t b = s;
    *state = s;

    const float ua = (float)(a >> 8) * (1.0f / 16777215.0f);
    const float ub = (float)(b >> 8) * (1.0f / 16777215.0f);
    return (ua + ub) - 1.0f;
}

static uint8_t mod_lfo_shape_is_positive(mod_lfo_shape_t shape)
{
    return ((shape == MOD_LFO_SHAPE_SINE_POS)
            || (shape == MOD_LFO_SHAPE_TRIANGLE_POS)
            || (shape == MOD_LFO_SHAPE_SQUARE_POS)) ? 1U : 0U;
}

static float mod_lfo_wave(mod_lfo_shape_t shape, uint32_t phase, mod_lfo_runtime_state_t *state)
{
    return mod_lfo_segment_wave((uint8_t)shape, phase, state->sh_value);
}

static void mod_lfo_prepare_ramp(mod_lfo_runtime_state_t *rt,
                                 mod_lfo_shape_t shape,
                                 mod_lfo_trig_mode_t trig,
                                 uint32_t frames)
{
    if ((rt == NULL) || (frames == 0U))
    {
        return;
    }

    rt->ramp_valid = 0U;
    rt->ramp_end = 0.0f;
    rt->ramp_discontinuous = 0U;
    uint32_t remaining = frames;
    const uint8_t split_policy = mod_lfo_segment_policy_from_shape(
        (uint8_t)shape, (trig == MOD_LFO_TRIG_ONE) ? 1U : 0U);
    while (remaining > 0U)
    {
        if ((shape == MOD_LFO_SHAPE_RANDOM_SH) && (rt->sh_valid == 0U))
        {
            rt->sh_value = mod_lfo_sh_next_value(&rt->rng_state);
            rt->sh_valid = 1U;
        }

        mod_lfo_ramp_t ramp;
        const uint32_t consumed = mod_lfo_segment_plan((uint8_t)shape,
                                                        rt->phase,
                                                        rt->phase_inc,
                                                        remaining,
                                                        rt->sh_value,
                                                        split_policy,
                                                        &ramp);
        if (consumed == 0U)
        {
            break;
        }

        if (rt->ramp_valid == 0U)
        {
            rt->ramp = ramp;
            rt->ramp_valid = 1U;
        }
        rt->ramp_end = ramp.start + (ramp.step * (float)(ramp.frames - 1U));
        rt->ramp_discontinuous = (uint8_t)(rt->ramp_discontinuous || (ramp.transition != 0U));

        const uint32_t phase_before = rt->phase;
        rt->phase = ramp.phase_after;
        remaining -= consumed;

        if ((shape == MOD_LFO_SHAPE_RANDOM_SH) && (rt->phase < phase_before))
        {
            rt->sh_value = mod_lfo_sh_next_value(&rt->rng_state);
            rt->sh_valid = 1U;
        }

        if ((trig == MOD_LFO_TRIG_ONE) && (rt->phase < phase_before))
        {
            rt->one_done = 1U;
            rt->one_running = 0U;
            rt->phase = 0U;
            break;
        }
    }
}

static void mod_lfo_capture_hold_value(mod_lfo_runtime_state_t *rt, mod_lfo_shape_t shape)
{
    if (rt == NULL)
    {
        return;
    }

    if (shape == MOD_LFO_SHAPE_RANDOM_SH)
    {
        rt->sh_value = mod_lfo_sh_next_value(&rt->rng_state);
        rt->sh_valid = 1U;
    }

    rt->hold_value = mod_lfo_wave(shape, rt->phase, rt);
    rt->hold_valid = 1U;
}

static float mod_lfo_poly_random_slew_coeff(float phase_setting,
                                            uint32_t frames)
{
    const float slew_norm = mod_lfo_clampf(phase_setting / 360.0f, 0.0f, 1.0f);
    const float reference_coeff = 1.0f - (slew_norm * 0.95f);
    if (reference_coeff >= 1.0f)
    {
        return 1.0f;
    }

    const float time_constant_frames =
        64.0f * (1.0f - reference_coeff) / reference_coeff;
    return (float)frames / (time_constant_frames + (float)frames);
}

static void mod_lfo_clear_poly_segment_config(void)
{
    for (uint8_t i = 0U; i < g_mod_lfo_poly_prepared_entry_count; ++i)
    {
        const uint8_t entry = g_mod_lfo_poly_prepared_entries[i];
        const uint8_t track = (uint8_t)(entry / MOD_LFO_COUNT_PER_TRACK);
        const uint8_t lfo = (uint8_t)(entry % MOD_LFO_COUNT_PER_TRACK);
        g_mod_lfo_poly_segment_config[track][lfo].valid = 0U;
    }
    g_mod_lfo_poly_prepared_entry_count = 0U;
}

static void mod_lfo_prepare_poly_segment(uint32_t frames,
                                         uint32_t bpm_milli)
{
    mod_lfo_clear_poly_segment_config();

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const uint8_t poly_mask = mod_matrix_poly_route_mask(track);
        if (poly_mask == 0U)
        {
            continue;
        }

        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            if ((poly_mask & (uint8_t)(1U << lfo)) == 0U)
            {
                continue;
            }

            const track_mod_lfo_state_t *const s =
                mod_lfo_track_settings_const(track, lfo);
            if (s == NULL)
            {
                continue;
            }

            const mod_lfo_runtime_state_t *const shared =
                &g_mod_lfo_runtime[track][lfo];
            const float rate = mod_lfo_effective_field(
                shared, s, MOD_LFO_PARAM_RATE);
            const float phase_setting = mod_lfo_effective_field(
                shared, s, MOD_LFO_PARAM_PHASE);
            const mod_lfo_shape_t shape = (mod_lfo_shape_t)((uint8_t)
                (mod_lfo_effective_field(shared, s, MOD_LFO_PARAM_SHAPE) + 0.5f));
            const mod_lfo_trig_mode_t trig = (mod_lfo_trig_mode_t)((uint8_t)
                (mod_lfo_effective_field(shared, s, MOD_LFO_PARAM_TRIG) + 0.5f));
            const uint32_t phase_inc =
                mod_lfo_phase_inc_from_rate_with_bpm(rate, bpm_milli);
            if ((trig < MOD_LFO_TRIG_POLY_TRIG) || (phase_inc == 0U))
            {
                continue;
            }

            mod_lfo_poly_segment_config_t *const prepared =
                &g_mod_lfo_poly_segment_config[track][lfo];
            prepared->phase_inc = phase_inc;
            prepared->frames = frames;
            prepared->random_slew_coeff = (shape == MOD_LFO_SHAPE_RANDOM_SH)
                ? mod_lfo_poly_random_slew_coeff(phase_setting, frames) : 1.0f;
            prepared->shape = (uint8_t)shape;
            prepared->flags = (uint8_t)
                (((shape == MOD_LFO_SHAPE_RANDOM_SH) ? MOD_LFO_POLY_FLAG_RANDOM : 0U)
                 | ((trig == MOD_LFO_TRIG_POLY_HOLD) ? MOD_LFO_POLY_FLAG_HOLD : 0U)
                 | ((trig == MOD_LFO_TRIG_POLY_ONE) ? MOD_LFO_POLY_FLAG_ONE : 0U)
                 | (mod_lfo_segment_policy_from_shape(
                        (uint8_t)shape,
                        (trig == MOD_LFO_TRIG_POLY_ONE) ? 1U : 0U)
                    << MOD_LFO_POLY_FLAG_SPLIT_SHIFT));
            prepared->valid = 1U;
            g_mod_lfo_poly_prepared_entries[g_mod_lfo_poly_prepared_entry_count++] =
                (uint8_t)(track * MOD_LFO_COUNT_PER_TRACK + lfo);
        }
    }
}

static void mod_lfo_process_control_tick(uint32_t elapsed_frames,
                                          uint32_t bpm_milli)
{
    if (param_registry_track_structure_transition_is_global_active() != 0U)
    {
        return;
    }

    if (mod_matrix_has_any_configured_route() == 0U)
    {
        if (g_mod_lfo_had_matrix_routes != 0U)
        {
            for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
            {
                for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
                {
                    g_mod_lfo_runtime[track][lfo].active = 0U;
                }
                g_mod_lfo_track_had_matrix_routes[track] = 0U;
            }
            g_mod_lfo_had_matrix_routes = 0U;
        }
        return;
    }

    g_mod_lfo_had_matrix_routes = 1U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (param_registry_track_structure_transition_is_track_active(track) != 0U)
        {
            continue;
        }

        if (mod_matrix_track_has_configured_route(track) == 0U)
        {
            if (g_mod_lfo_track_had_matrix_routes[track] != 0U)
            {
                for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
                {
                    g_mod_lfo_runtime[track][lfo].active = 0U;
                }
                g_mod_lfo_track_had_matrix_routes[track] = 0U;
            }
            continue;
        }
        g_mod_lfo_track_had_matrix_routes[track] = 1U;

        const track_audio_runtime_ctx_t *const ctx = audio_note_engine_adapter_audio_ctx(track);
        const ui_track_family_t family = mod_lfo_ui_family_from_ctx(ctx);
        const ui_track_type_t type = mod_lfo_ui_type_from_ctx(ctx);
        float source_values[MOD_MATRIX_SOURCE_COUNT] = {0.0f};
        float source_end[MOD_MATRIX_SOURCE_COUNT] = {0.0f};
        uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT] = {0U};
        uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT] = {0U};

        if (mod_matrix_source_has_active_route(track, MOD_MATRIX_SOURCE_ENV3, family, type, ctx) != 0U)
        {
            source_values[MOD_MATRIX_SOURCE_ENV3] = mod_env3_process_track(track, elapsed_frames);
            source_end[MOD_MATRIX_SOURCE_ENV3] = source_values[MOD_MATRIX_SOURCE_ENV3];
            source_valid[MOD_MATRIX_SOURCE_ENV3] = 1U;
        }
        if ((ctx != NULL)
                && (ctx->audio_binding.mix_track_id < MIXER_MAX_TRACKS)
                && (mod_matrix_source_has_active_route(track, MOD_MATRIX_SOURCE_ENV_VCA, family, type, ctx) != 0U))
        {
            source_values[MOD_MATRIX_SOURCE_ENV_VCA] = mixer_get_track_vca_env_value(ctx->audio_binding.mix_track_id);
            source_end[MOD_MATRIX_SOURCE_ENV_VCA] = source_values[MOD_MATRIX_SOURCE_ENV_VCA];
            source_valid[MOD_MATRIX_SOURCE_ENV_VCA] = 1U;
        }
        if (mod_matrix_source_has_active_route(track, MOD_MATRIX_SOURCE_ENV_FLT, family, type, ctx) != 0U)
        {
            if ((ctx != NULL)
                    && ((ctx->flags & 1U) != 0U)
                    && (ctx->audio_binding.mix_track_id < MIXER_MAX_TRACKS))
            {
                source_values[MOD_MATRIX_SOURCE_ENV_FLT] =
                    mixer_prepare_track_filter_env_source(
                        ctx->audio_binding.mix_track_id, elapsed_frames);
                source_end[MOD_MATRIX_SOURCE_ENV_FLT] =
                    source_values[MOD_MATRIX_SOURCE_ENV_FLT];
                source_valid[MOD_MATRIX_SOURCE_ENV_FLT] = 1U;
            }
        }

        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo);
            mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo];

            if (s == NULL)
            {
                continue;
            }

            const float shape = mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_SHAPE);
            const float trig_f = mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_TRIG);
            const float phase = mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_PHASE);
            const mod_lfo_shape_t shape_id = (mod_lfo_shape_t)((uint8_t)(shape + 0.5f));
            const mod_lfo_trig_mode_t trig = (mod_lfo_trig_mode_t)((uint8_t)(trig_f + 0.5f));
            const float rate = mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_RATE);
            uint32_t phase_inc = 0U;
            if (trig >= MOD_LFO_TRIG_POLY_TRIG)
            {
                const mod_lfo_poly_segment_config_t *const prepared =
                    &g_mod_lfo_poly_segment_config[track][lfo];
                phase_inc = (prepared->valid != 0U)
                    ? prepared->phase_inc
                    : mod_lfo_phase_inc_from_rate_with_bpm(rate, bpm_milli);
            }
            else
            {
                phase_inc = mod_lfo_phase_inc_from_rate_with_bpm(rate, bpm_milli);
            }
            if ((phase_inc == 0U)
                    || (mod_matrix_source_has_active_route(
                        track,
                        (mod_matrix_source_t)((uint8_t)MOD_MATRIX_SOURCE_LFO1 + lfo),
                        family,
                        type,
                        ctx) == 0U))
            {
                rt->active = 0U;
                continue;
            }

            if (trig >= MOD_LFO_TRIG_POLY_TRIG)
            {
                rt->active = 0U;
                continue;
            }

            rt->phase_inc = phase_inc;
            if (rt->phase_inc == 0U)
            {
                rt->active = 0U;
                continue;
            }

            if (rt->active == 0U)
            {
                rt->active = 1U;
                rt->one_done = 0U;
                rt->one_running = (trig == MOD_LFO_TRIG_ONE) ? 1U : 0U;
                mod_lfo_start_phase(rt, shape_id, phase);
                if (trig == MOD_LFO_TRIG_HOLD)
                {
                    mod_lfo_capture_hold_value(rt, shape_id);
                }
            }

            if ((trig == MOD_LFO_TRIG_ONE) && (rt->one_done != 0U))
            {
                rt->current = mod_lfo_wave(shape_id, 0xFFFFFFFFU, rt);
                source_values[(uint8_t)MOD_MATRIX_SOURCE_LFO1 + lfo] = rt->current;
                source_end[(uint8_t)MOD_MATRIX_SOURCE_LFO1 + lfo] = rt->current;
                source_valid[(uint8_t)MOD_MATRIX_SOURCE_LFO1 + lfo] = 1U;
                continue;
            }

            mod_lfo_prepare_ramp(rt, shape_id, trig, elapsed_frames);

            if ((trig == MOD_LFO_TRIG_HOLD) && (rt->hold_valid != 0U))
            {
                rt->current = rt->hold_value;
                rt->ramp_end = rt->hold_value;
                rt->ramp_discontinuous = 0U;
            }
            else
            {
                rt->current = (rt->ramp_valid != 0U)
                    ? rt->ramp.start
                    : mod_lfo_wave(shape_id, rt->phase, rt);
            }

            if (shape_id == MOD_LFO_SHAPE_RANDOM_SH)
            {
                const float slew_norm = mod_lfo_clampf(phase / 360.0f, 0.0f, 1.0f);
                const float reference_coeff = 1.0f - (slew_norm * 0.95f);
                float coeff = 1.0f;
                if (reference_coeff < 1.0f)
                {
                    const float time_constant_frames =
                        64.0f * (1.0f - reference_coeff) / reference_coeff;
                    coeff = (float)elapsed_frames
                        / (time_constant_frames + (float)elapsed_frames);
                }
                if (rt->slew_valid == 0U)
                {
                    rt->slew_value = rt->current;
                    rt->slew_valid = 1U;
                }
                else
                {
                    rt->slew_value += (rt->current - rt->slew_value) * coeff;
                }
                rt->current = rt->slew_value;
            }

            const uint8_t source = (uint8_t)MOD_MATRIX_SOURCE_LFO1 + lfo;
            source_values[source] = rt->current;
            source_end[source] = (shape_id == MOD_LFO_SHAPE_RANDOM_SH)
                ? rt->current
                : rt->ramp_end;
            source_valid[source] = 1U;
            source_discontinuous[source] = rt->ramp_discontinuous;
        }

        mod_matrix_process_operators_ramped(track,
                                            source_values,
                                            source_end,
                                            source_valid,
                                            source_discontinuous,
                                            elapsed_frames);
        mod_matrix_process_track_ramped(track,
                                        ctx,
                                        source_values,
                                        source_end,
                                        source_valid,
                                        source_discontinuous,
                                        elapsed_frames);
    }
}

void mod_lfo_v1_init(void)
{
    memset(g_mod_lfo_runtime, 0, sizeof(g_mod_lfo_runtime));
    memset(g_mod_lfo_poly_runtime, 0, sizeof(g_mod_lfo_poly_runtime));
    memset(g_mod_lfo_poly_owner, SYNTH_POLYPHONY_NO_VOICE, sizeof(g_mod_lfo_poly_owner));
    memset(g_mod_lfo_poly_segment_config,
           0,
           sizeof(g_mod_lfo_poly_segment_config));
    memset(g_mod_lfo_poly_prepared_entries,
           0,
           sizeof(g_mod_lfo_poly_prepared_entries));
    g_mod_lfo_poly_prepared_entry_count = 0U;
    mod_destination_catalog_init();
    mod_env3_init();
    mod_matrix_init();
    g_mod_lfo_control_counter = 0U;
    g_mod_lfo_had_matrix_routes = 0U;
    memset(g_mod_lfo_track_had_matrix_routes, 0, sizeof(g_mod_lfo_track_had_matrix_routes));

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            g_mod_lfo_runtime[track][lfo].phase = 0U;
            g_mod_lfo_runtime[track][lfo].phase_inc = 1U;
            g_mod_lfo_runtime[track][lfo].current = 0.0f;
            g_mod_lfo_runtime[track][lfo].hold_value = 0.0f;
            g_mod_lfo_runtime[track][lfo].slew_value = 0.0f;
            g_mod_lfo_runtime[track][lfo].rng_state = 0xA341316CU ^ ((uint32_t)track << 8) ^ (uint32_t)lfo;
            g_mod_lfo_runtime[track][lfo].sh_value = 0.0f;
            g_mod_lfo_runtime[track][lfo].sh_valid = 0U;
            g_mod_lfo_runtime[track][lfo].triggered = 0U;
            g_mod_lfo_runtime[track][lfo].one_running = 0U;
            g_mod_lfo_runtime[track][lfo].one_done = 0U;
            g_mod_lfo_runtime[track][lfo].hold_valid = 0U;
            g_mod_lfo_runtime[track][lfo].slew_valid = 0U;
            g_mod_lfo_runtime[track][lfo].active = 0U;
            g_mod_lfo_runtime[track][lfo].ramp_valid = 0U;
            g_mod_lfo_runtime[track][lfo].ramp_end = 0.0f;
            g_mod_lfo_runtime[track][lfo].ramp_discontinuous = 0U;
            g_mod_lfo_runtime[track][lfo].temp_valid_mask = 0U;
            g_mod_lfo_runtime[track][lfo].temp = (track_mod_lfo_state_t){0};
        }
    }

    mod_lfo_v1_invalidate_dest_cache_all();
}

void mod_lfo_v1_reset_runtime(void)
{
    g_mod_lfo_control_counter = 0U;
    g_mod_lfo_had_matrix_routes = 0U;
    memset(g_mod_lfo_track_had_matrix_routes, 0, sizeof(g_mod_lfo_track_had_matrix_routes));
    mod_destination_catalog_reset_runtime();
    mod_env3_reset_runtime();
    mod_matrix_reset_runtime();
    memset(g_mod_lfo_poly_runtime, 0, sizeof(g_mod_lfo_poly_runtime));
    memset(g_mod_lfo_poly_owner, SYNTH_POLYPHONY_NO_VOICE, sizeof(g_mod_lfo_poly_owner));
    memset(g_mod_lfo_poly_segment_config,
           0,
           sizeof(g_mod_lfo_poly_segment_config));
    memset(g_mod_lfo_poly_prepared_entries,
           0,
           sizeof(g_mod_lfo_poly_prepared_entries));
    g_mod_lfo_poly_prepared_entry_count = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            g_mod_lfo_runtime[track][lfo].phase = 0U;
            {
                const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo);
                const float rate = (s != NULL) ? s->rate : 0.0f;
                g_mod_lfo_runtime[track][lfo].phase_inc = mod_lfo_phase_inc_from_rate(rate);
            }
            g_mod_lfo_runtime[track][lfo].current = 0.0f;
            g_mod_lfo_runtime[track][lfo].sh_valid = 0U;
            g_mod_lfo_runtime[track][lfo].triggered = 0U;
            g_mod_lfo_runtime[track][lfo].one_running = 0U;
            g_mod_lfo_runtime[track][lfo].one_done = 0U;
            g_mod_lfo_runtime[track][lfo].hold_valid = 0U;
            g_mod_lfo_runtime[track][lfo].slew_valid = 0U;
            g_mod_lfo_runtime[track][lfo].active = 0U;
            g_mod_lfo_runtime[track][lfo].ramp_valid = 0U;
            g_mod_lfo_runtime[track][lfo].ramp_end = 0.0f;
            g_mod_lfo_runtime[track][lfo].ramp_discontinuous = 0U;
            g_mod_lfo_runtime[track][lfo].temp_valid_mask = 0U;
        }
    }

    mod_lfo_v1_invalidate_dest_cache_all();
}

uint8_t mod_lfo_v1_set_track_param(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return 0U;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    track = owner;
    track_mod_lfo_state_t *const s = mod_lfo_track_settings_mut(track, lfo_index);
    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];

    if (s == NULL)
    {
        return 0U;
    }

    switch (param)
    {
        case MOD_LFO_PARAM_RATE:
            rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
            s->rate = mod_lfo_clampf(value, -LFO_FREE_MAX_HZ, (float)MOD_LFO_SYNC_RATE_COUNT);
            if (s->rate > 0.0f)
            {
                s->rate = mod_lfo_quantize_sync_rate(s->rate);
            }
            rt->phase_inc = mod_lfo_phase_inc_from_rate(s->rate);
            rt->ramp_valid = 0U;
            if (rt->phase_inc == 0U)
            {
                rt->active = 0U;
            }
            return 1U;

        case MOD_LFO_PARAM_SHAPE:
            rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
            s->shape = mod_lfo_clampf(value, 0.0f, (float)((uint8_t)MOD_LFO_SHAPE_COUNT - 1U));
            rt->sh_valid = 0U;
            rt->slew_valid = 0U;
            rt->ramp_valid = 0U;
            return 1U;

        case MOD_LFO_PARAM_TRIG:
        {
            const mod_lfo_trig_mode_t old_trig = (mod_lfo_trig_mode_t)((uint8_t)
                (mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_TRIG) + 0.5f));
            rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
            s->trig = mod_lfo_clampf(value, 0.0f, (float)((uint8_t)MOD_LFO_TRIG_COUNT - 1U));
            rt->triggered = 0U;
            rt->one_running = 0U;
            rt->one_done = 0U;
            rt->hold_valid = 0U;
            rt->active = 0U;
            rt->ramp_valid = 0U;
            mod_lfo_poly_mode_changed(track,
                                      lfo_index,
                                      old_trig,
                                      (mod_lfo_trig_mode_t)((uint8_t)(s->trig + 0.5f)));
            return 1U;
        }

        case MOD_LFO_PARAM_PHASE:
            rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
            s->phase = mod_lfo_clampf(value, 0.0f, 360.0f);
            rt->slew_valid = 0U;
            rt->ramp_valid = 0U;
            return 1U;

        default:
            return 0U;
    }
}

uint8_t mod_lfo_v1_apply_track_param_temp(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return 0U;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    track = owner;
    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];
    if (s == NULL)
    {
        return 0U;
    }

    if (rt->temp_valid_mask == 0U)
    {
        rt->temp = *s;
    }

    switch (param)
    {
        case MOD_LFO_PARAM_RATE:
            rt->temp.rate = mod_lfo_clampf(value, -LFO_FREE_MAX_HZ, (float)MOD_LFO_SYNC_RATE_COUNT);
            if (rt->temp.rate > 0.0f)
            {
                rt->temp.rate = mod_lfo_quantize_sync_rate(rt->temp.rate);
            }
            rt->phase_inc = mod_lfo_phase_inc_from_rate(rt->temp.rate);
            if (rt->phase_inc == 0U)
            {
                rt->active = 0U;
            }
            break;

        case MOD_LFO_PARAM_SHAPE:
            rt->temp.shape = mod_lfo_clampf(value, 0.0f, (float)((uint8_t)MOD_LFO_SHAPE_COUNT - 1U));
            rt->sh_valid = 0U;
            rt->slew_valid = 0U;
            break;

        case MOD_LFO_PARAM_TRIG:
        {
            const mod_lfo_trig_mode_t old_trig = (mod_lfo_trig_mode_t)((uint8_t)
                (mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_TRIG) + 0.5f));
            rt->temp.trig = mod_lfo_clampf(value, 0.0f, (float)((uint8_t)MOD_LFO_TRIG_COUNT - 1U));
            rt->triggered = 0U;
            rt->one_running = 0U;
            rt->one_done = 0U;
            rt->hold_valid = 0U;
            rt->active = 0U;
            mod_lfo_poly_mode_changed(track,
                                      lfo_index,
                                      old_trig,
                                      (mod_lfo_trig_mode_t)((uint8_t)(rt->temp.trig + 0.5f)));
            break;
        }

        case MOD_LFO_PARAM_PHASE:
            rt->temp.phase = mod_lfo_clampf(value, 0.0f, 360.0f);
            rt->slew_valid = 0U;
            break;

        default:
            return 0U;
    }

    rt->temp_valid_mask |= mod_lfo_runtime_param_mask(param);
    return 1U;
}

void mod_lfo_v1_clear_track_param_temp(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return;
    track = owner;
    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];
    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    const mod_lfo_trig_mode_t old_trig = (s != NULL)
        ? (mod_lfo_trig_mode_t)((uint8_t)(mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_TRIG) + 0.5f))
        : MOD_LFO_TRIG_FREE;
    rt->temp_valid_mask &= (uint8_t)~mod_lfo_runtime_param_mask(param);
    if ((param == MOD_LFO_PARAM_TRIG) && (s != NULL))
    {
        const mod_lfo_trig_mode_t new_trig = (mod_lfo_trig_mode_t)((uint8_t)(s->trig + 0.5f));
        mod_lfo_poly_mode_changed(track, lfo_index, old_trig, new_trig);
    }
}

mod_lfo_trig_mode_t mod_lfo_v1_effective_trig(uint8_t track, uint8_t lfo_index)
{
    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    if ((s == NULL) || (track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK))
        return MOD_LFO_TRIG_FREE;
    return (mod_lfo_trig_mode_t)((uint8_t)(mod_lfo_effective_field(
        &g_mod_lfo_runtime[track][lfo_index], s, MOD_LFO_PARAM_TRIG) + 0.5f));
}

uint8_t mod_lfo_v1_get_track_param(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float *out_value)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || (out_value == NULL)
            || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return 0U;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    track = owner;
    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    if (s == NULL)
    {
        return 0U;
    }

    switch (param)
    {
        case MOD_LFO_PARAM_RATE:
            *out_value = s->rate;
            return 1U;

        case MOD_LFO_PARAM_SHAPE:
            *out_value = s->shape;
            return 1U;

        case MOD_LFO_PARAM_TRIG:
            *out_value = s->trig;
            return 1U;

        case MOD_LFO_PARAM_PHASE:
            *out_value = s->phase;
            return 1U;

        default:
            return 0U;
    }
}

void mod_lfo_v1_resync_base_on_authoritative_write(uint8_t track, param_id_t id, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (id >= PARAM_COUNT))
    {
        return;
    }

    mod_destination_catalog_invalidate_runtime_value(track, id);
    mod_matrix_resync_base_on_authoritative_write(track, id, value);
}

void mod_lfo_v1_process_sample_all(void)
{
    mod_lfo_v1_process_block(1U);
}

ITCM_AUDIT_32_TEXT void mod_lfo_v1_process_block(uint32_t frames)
{
    if (frames == 0U)
    {
        return;
    }

    const uint32_t bpm_milli = seq_runtime_get_tempo_bpm_milli();
    mod_lfo_prepare_poly_segment(frames, bpm_milli);

#if MOD_LFO_WINDOW_RATE_EXPERIMENT
    g_mod_lfo_control_counter = 0U;
    mod_lfo_process_control_tick(frames, bpm_milli);
#else
    g_mod_lfo_control_counter += frames;
    while (g_mod_lfo_control_counter >= MOD_LFO_CONTROL_STRIDE)
    {
        g_mod_lfo_control_counter -= MOD_LFO_CONTROL_STRIDE;
        mod_lfo_process_control_tick(1U, bpm_milli);
    }
#endif
}

void mod_lfo_v1_note_trigger(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    mod_env3_note_on(track);

    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo);
        mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo];
        if (s == NULL)
        {
            continue;
        }

        const mod_lfo_trig_mode_t trig = (mod_lfo_trig_mode_t)((uint8_t)(s->trig + 0.5f));
        const mod_lfo_shape_t shape = (mod_lfo_shape_t)((uint8_t)(s->shape + 0.5f));
        if ((trig == MOD_LFO_TRIG_FREE) || (trig >= MOD_LFO_TRIG_POLY_TRIG))
        {
            continue;
        }

        rt->triggered = 1U;
        rt->one_done = 0U;
        rt->one_running = (trig == MOD_LFO_TRIG_ONE) ? 1U : 0U;

        if ((trig == MOD_LFO_TRIG_TRIG) || (trig == MOD_LFO_TRIG_ONE))
        {
            mod_lfo_start_phase(rt, shape, s->phase);
        }
        else if (trig == MOD_LFO_TRIG_HOLD)
        {
            mod_lfo_capture_hold_value(rt, shape);
        }
    }
}

void mod_lfo_v1_poly_voice_reset(uint8_t voice_slot)
{
    if (voice_slot < MOD_LFO_POLY_SLOT_COUNT)
    {
        memset(g_mod_lfo_poly_runtime[voice_slot], 0,
               sizeof(g_mod_lfo_poly_runtime[voice_slot]));
        g_mod_lfo_poly_owner[voice_slot] = SYNTH_POLYPHONY_NO_VOICE;
    }
}

void mod_lfo_v1_poly_note_trigger(uint8_t track, uint8_t voice_slot)
{
    if ((track >= SEQ_TRACK_COUNT) || (voice_slot >= MOD_LFO_POLY_SLOT_COUNT))
    {
        return;
    }

    g_mod_lfo_poly_owner[voice_slot] = track;

    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo);
        if (s == NULL) continue;
        const mod_lfo_runtime_state_t *const shared = &g_mod_lfo_runtime[track][lfo];
        const mod_lfo_trig_mode_t trig = (mod_lfo_trig_mode_t)((uint8_t)
            (mod_lfo_effective_field(shared, s, MOD_LFO_PARAM_TRIG) + 0.5f));
        mod_lfo_poly_state_t *const rt = &g_mod_lfo_poly_runtime[voice_slot][lfo];
        if (trig < MOD_LFO_TRIG_POLY_TRIG)
        {
            mod_lfo_poly_state_invalidate(rt, 0U);
            continue;
        }

        const mod_lfo_shape_t shape = (mod_lfo_shape_t)((uint8_t)
            (mod_lfo_effective_field(shared, s, MOD_LFO_PARAM_SHAPE) + 0.5f));
        rt->one_done = 0U;
        rt->hold_valid = 0U;
        rt->pending_trigger = 0U;
        rt->pending_reset = 0U;
        if (rt->rng_state == 0U)
        {
            rt->rng_state = 0xA341316CU ^ ((uint32_t)voice_slot << 8) ^ (uint32_t)lfo;
        }
        if ((trig != MOD_LFO_TRIG_POLY_HOLD) || (rt->active == 0U))
        {
            rt->slew_valid = 0U;
            rt->phase = (shape == MOD_LFO_SHAPE_RANDOM_SH) ? 0U
                : mod_lfo_phase_from_degrees(
                    mod_lfo_effective_field(shared, s, MOD_LFO_PARAM_PHASE));
            rt->sh_valid = 0U;
        }
        rt->active = 1U;
        if (trig == MOD_LFO_TRIG_POLY_HOLD)
        {
            if (shape == MOD_LFO_SHAPE_RANDOM_SH)
            {
                rt->sh_value = mod_lfo_sh_next_value(&rt->rng_state);
                rt->sh_valid = 1U;
            }
            rt->hold_value = mod_lfo_segment_wave((uint8_t)shape, rt->phase, rt->sh_value);
            rt->hold_valid = 1U;
        }
    }
}

void mod_lfo_v1_process_poly_voice(uint8_t track,
                                   uint8_t voice_slot,
                                   const track_audio_runtime_ctx_t *ctx,
                                   uint32_t frames)
{
    if ((track >= SEQ_TRACK_COUNT) || (voice_slot >= MOD_LFO_POLY_SLOT_COUNT)
            || (ctx == NULL) || (frames == 0U))
    {
        return;
    }
    const uint8_t poly_mask = mod_matrix_poly_route_mask(track);
    if (poly_mask == 0U) return;

    float source_start[MOD_MATRIX_SOURCE_COUNT] = {0.0f};
    float source_end[MOD_MATRIX_SOURCE_COUNT] = {0.0f};
    uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT] = {0U};
    uint8_t any_valid = 0U;
    uint8_t pending_reset = 0U;
    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        if ((poly_mask & (uint8_t)(1U << lfo)) != 0U
                && (g_mod_lfo_poly_runtime[voice_slot][lfo].pending_reset != 0U))
        {
            pending_reset = 1U;
            break;
        }
    }
    if (pending_reset != 0U)
    {
        mod_matrix_reset_poly_voice(track, voice_slot, ctx);
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            g_mod_lfo_poly_runtime[voice_slot][lfo].pending_reset = 0U;
        }
    }

    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        if ((poly_mask & (uint8_t)(1U << lfo)) == 0U) continue;
        const mod_lfo_poly_segment_config_t *const prepared =
            &g_mod_lfo_poly_segment_config[track][lfo];
        if ((prepared->valid == 0U) || (prepared->frames != frames)) continue;
        const uint8_t source = (uint8_t)MOD_MATRIX_SOURCE_LFO1 + lfo;
        mod_lfo_poly_state_t *const rt = &g_mod_lfo_poly_runtime[voice_slot][lfo];
        const mod_lfo_shape_t shape = (mod_lfo_shape_t)prepared->shape;
        const uint32_t phase_inc = prepared->phase_inc;
        if (rt->pending_trigger != 0U)
        {
            continue;
        }
        if (phase_inc == 0U) continue;
        if (rt->active == 0U)
        {
            mod_lfo_v1_poly_note_trigger(track, voice_slot);
        }

        if (((prepared->flags & MOD_LFO_POLY_FLAG_RANDOM) != 0U)
                && (rt->sh_valid == 0U))
        {
            rt->sh_value = mod_lfo_sh_next_value(&rt->rng_state);
            rt->sh_valid = 1U;
        }
        float start = 0.0f;
        float end = 0.0f;
        if (((prepared->flags & MOD_LFO_POLY_FLAG_ONE) != 0U)
                && (rt->one_done != 0U))
        {
            start = mod_lfo_segment_wave((uint8_t)shape, 0xFFFFFFFFU, rt->sh_value);
            end = start;
        }
        else
        {
            uint32_t remaining = frames;
            uint8_t first = 1U;
            while (remaining != 0U)
            {
                mod_lfo_ramp_t ramp;
                const uint32_t phase_before = rt->phase;
                const uint32_t consumed = mod_lfo_segment_plan((uint8_t)shape, rt->phase,
                                                                phase_inc, remaining, rt->sh_value,
                                                                (uint8_t)((prepared->flags
                                                                           & MOD_LFO_POLY_FLAG_SPLIT_MASK)
                                                                          >> MOD_LFO_POLY_FLAG_SPLIT_SHIFT),
                                                                &ramp);
                if (consumed == 0U)
                {
                    if (first != 0U)
                    {
                        start = mod_lfo_segment_wave((uint8_t)shape,
                                                     rt->phase,
                                                     rt->sh_value);
                        end = start;
                    }
                    break;
                }
                if (first != 0U)
                {
                    start = ramp.start;
                    first = 0U;
                }
                end = ramp.start + ramp.step * (float)(ramp.frames - 1U);
                rt->phase = ramp.phase_after;
                remaining -= consumed;
                if (((prepared->flags & MOD_LFO_POLY_FLAG_RANDOM) != 0U)
                        && (rt->phase < phase_before))
                {
                    rt->sh_value = mod_lfo_sh_next_value(&rt->rng_state);
                }
                if (((prepared->flags & MOD_LFO_POLY_FLAG_ONE) != 0U)
                        && (rt->phase < phase_before))
                {
                    rt->one_done = 1U;
                    rt->phase = 0U;
                    break;
                }
            }
        }
        if (((prepared->flags & MOD_LFO_POLY_FLAG_HOLD) != 0U)
                && (rt->hold_valid != 0U))
        {
            start = rt->hold_value;
            end = start;
        }
        else if ((prepared->flags & MOD_LFO_POLY_FLAG_RANDOM) != 0U)
        {
            if (rt->slew_valid == 0U)
            {
                rt->slew_value = start;
                rt->slew_valid = 1U;
            }
            else
            {
                rt->slew_value += (start - rt->slew_value)
                    * prepared->random_slew_coeff;
            }
            start = rt->slew_value;
            end = start;
        }
        source_start[source] = start;
        source_end[source] = end;
        source_valid[source] = 1U;
        any_valid = 1U;
    }

    if (any_valid != 0U)
    {
        mod_matrix_process_poly_voice_ramped(track, voice_slot, ctx,
                                             source_start, source_end, source_valid);
    }
}

void mod_lfo_v1_note_release(uint8_t track)
{
    mod_env3_note_off(track);
}

void mod_lfo_v1_all_notes_off(uint8_t track)
{
    mod_env3_all_notes_off(track);
}

uint8_t mod_lfo_v1_shape_is_random(uint8_t track, uint8_t lfo_index)
{
    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    track = owner;
    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    if (s == NULL)
    {
        return 0U;
    }
    return ((uint8_t)(s->shape + 0.5f) == (uint8_t)MOD_LFO_SHAPE_RANDOM_SH) ? 1U : 0U;
}

uint8_t mod_lfo_v1_waveform_point(uint8_t track, uint8_t lfo_index, uint8_t x, uint8_t width, int8_t *out_y_q7)
{
    if ((out_y_q7 == NULL) || (width == 0U))
    {
        return 0U;
    }

    brick_entity_id_t owner = track;
    if (entity_topology_mod_owner(track, &owner) == 0U) return 0U;
    track = owner;
    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    if (s == NULL)
    {
        return 0U;
    }

    const mod_lfo_shape_t shape = (mod_lfo_shape_t)((uint8_t)(s->shape + 0.5f));
    if (shape == MOD_LFO_SHAPE_RANDOM_SH)
    {
        const int8_t rnd_pattern[8] = {-48, 32, 84, -16, -80, 4, 56, -28};
        *out_y_q7 = rnd_pattern[(width > 1U) ? ((x * 8U) / width) & 7U : 0U];
        return 1U;
    }

    mod_lfo_runtime_state_t preview = {0};
    preview.sh_value = 0.0f;
    const uint32_t phase = (uint32_t)(((uint64_t)x * 4294967296ULL) / (uint64_t)width);
    float y = mod_lfo_wave(shape, phase, &preview);
    if (mod_lfo_shape_is_positive(shape) != 0U)
    {
        y = (y * 2.0f) - 1.0f;
    }
    y = mod_lfo_clampf(y, -1.0f, 1.0f);
    *out_y_q7 = (int8_t)(y * 63.0f);
    return 1U;
}

uint16_t mod_lfo_v1_dest_count(uint8_t track)
{
    return mod_destination_catalog_count(track);
}

uint8_t mod_lfo_v1_dest_param_at(uint8_t track, uint16_t dest_index, param_id_t *out_param)
{
    if (out_param == NULL)
    {
        return 0U;
    }

    *out_param = mod_destination_catalog_param_from_index(track, dest_index);
    return 1U;
}

void mod_lfo_v1_invalidate_dest_cache_track(uint8_t track)
{
    mod_destination_catalog_invalidate_track(track);
    mod_matrix_rebuild_route_cache_track(track);
}

void mod_lfo_v1_invalidate_dest_cache_all(void)
{
    mod_destination_catalog_invalidate_all();
    mod_matrix_rebuild_route_cache_all();
}

uint8_t mod_lfo_v1_dest_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len)
{
    return mod_destination_catalog_label(track, dest_index, out, out_len);
}

uint8_t mod_lfo_v1_dest_short_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len)
{
    return mod_destination_catalog_short_label(track, dest_index, out, out_len);
}
