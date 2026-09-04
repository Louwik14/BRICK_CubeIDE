#include "Mod/mod_lfo_v1_audio.h"
#include "Audio/audio_note_engine_adapter.h"
#include "IPC/control_audio_command.h"
#include "Mod/mod_lfo_segment.h"
#include "Platform/memory_layout.h"
#include "Track/entity_types.h"
#include "stm32h7xx.h"

#include <math.h>
#include <string.h>

#include "Audio/brick6_audio_event_grid.h"
#include "Track/synth_polyphony.h"
#include "Audio/mixer.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_matrix_audio.h"
#include "Audio/audio_transport_runtime.h"

/* GROUP modulation state is owned by its master entity. */
#undef SEQ_TRACK_COUNT
#define SEQ_TRACK_COUNT BRICK_ENTITY_CAPACITY

#define MOD_LFO_AUDIO_SAMPLE_RATE 48000.0f
#define MOD_LFO_CONTROL_RATE_HZ 3000.0f
#define MOD_LFO_BASE_CONTROL_STRIDE ((uint32_t)(MOD_LFO_AUDIO_SAMPLE_RATE / MOD_LFO_CONTROL_RATE_HZ))
#define MOD_LFO_WINDOW_RATE_EXPERIMENT 1U
#ifndef MOD_LFO_WINDOW_RATE_FRAMES
#define MOD_LFO_WINDOW_RATE_FRAMES BRICK6_AUDIO_EVENT_GRID_FRAMES
#endif
#if MOD_LFO_WINDOW_RATE_EXPERIMENT
#define MOD_LFO_CONTROL_STRIDE ((uint32_t)MOD_LFO_WINDOW_RATE_FRAMES)
#define MOD_LFO_PHASE_DT (1.0f / MOD_LFO_AUDIO_SAMPLE_RATE)
#else
#define MOD_LFO_CONTROL_STRIDE MOD_LFO_BASE_CONTROL_STRIDE
#define MOD_LFO_PHASE_DT (1.0f / MOD_LFO_CONTROL_RATE_HZ)
#endif
#define MOD_LFO_RATE_OFF_EPS 0.0001f

static uint8_t mod_lfo_audio_resolve_owner(uint8_t track, uint8_t *out_owner)
{
    track_audio_runtime_ctx_t ctx;
    if ((out_owner == NULL) || !audio_note_engine_adapter_current_ctx(track, &ctx))
        return 0U;
    if ((ctx.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD) == 0U)
    { *out_owner = track; return 1U; }
    for (uint8_t entity = 0U; entity < SEQ_TRACK_COUNT; ++entity)
        if (audio_note_engine_adapter_current_ctx(entity, &ctx)
                && ((ctx.flags & CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER) != 0U))
        { *out_owner = entity; return 1U; }
    return 0U;
}

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
    float rate;
    float shape;
    float trig;
    float phase;
} track_mod_lfo_state_t;

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
static track_mod_lfo_state_t
    g_mod_lfo_audio_config[SEQ_TRACK_COUNT][MOD_LFO_COUNT_PER_TRACK];

#define MOD_LFO_SNAPSHOT_RESET_SHAPE  (1U << 0)
#define MOD_LFO_SNAPSHOT_RESET_TRIGGER (1U << 1)
static uint8_t g_mod_lfo_audio_initialized;

static const track_mod_lfo_state_t *mod_lfo_audio_settings_const(uint8_t track,
                                                                  uint8_t lfo_index)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK))
    {
        return NULL;
    }
    return &g_mod_lfo_audio_config[track][lfo_index];
}

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

static void mod_lfo_audio_reset_poly_track_lfo(uint8_t track,
                                               uint8_t lfo_index,
                                               uint8_t wait_for_trigger)
{
    const mod_lfo_trig_mode_t new_trig = (mod_lfo_trig_mode_t)(
        (uint8_t)(g_mod_lfo_audio_config[track][lfo_index].trig + 0.5f));
    const uint8_t wait = (uint8_t)(wait_for_trigger
        || (mod_lfo_trig_is_poly(new_trig) != 0U));
    for (uint8_t voice_slot = 0U; voice_slot < MOD_LFO_POLY_SLOT_COUNT; ++voice_slot)
    {
        if (g_mod_lfo_poly_owner[voice_slot] == track)
        {
            mod_lfo_poly_state_invalidate(
                &g_mod_lfo_poly_runtime[voice_slot][lfo_index], wait);
        }
    }
}

void mod_lfo_v1_audio_init(void)
{
    memset(g_mod_lfo_audio_config, 0, sizeof(g_mod_lfo_audio_config));
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
    g_mod_lfo_control_counter = 0U;
    g_mod_lfo_had_matrix_routes = 0U;
    memset(g_mod_lfo_track_had_matrix_routes, 0,
           sizeof(g_mod_lfo_track_had_matrix_routes));

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            g_mod_lfo_runtime[track][lfo].phase_inc = 1U;
            g_mod_lfo_runtime[track][lfo].rng_state =
                0xA341316CU ^ ((uint32_t)track << 8) ^ (uint32_t)lfo;
        }
    }
    g_mod_lfo_audio_initialized = 1U;
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
    return mod_lfo_phase_inc_from_rate_with_bpm(
        rate, audio_transport_runtime_get()->tempo_effective_bpm_milli);
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
                mod_lfo_audio_settings_const(track, lfo);
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

        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
                ? &ctx_value : NULL;
        const uint16_t required_source_mask =
            mod_matrix_required_source_mask(track);
        float source_values[MOD_MATRIX_SOURCE_COUNT] = {0.0f};
        float source_end[MOD_MATRIX_SOURCE_COUNT] = {0.0f};
        uint8_t source_valid[MOD_MATRIX_SOURCE_COUNT] = {0U};
        uint8_t source_discontinuous[MOD_MATRIX_SOURCE_COUNT] = {0U};

        if ((required_source_mask
             & (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_ENV3)) != 0U)
        {
            source_values[MOD_MATRIX_SOURCE_ENV3] = mod_env3_process_track(track, elapsed_frames);
            source_end[MOD_MATRIX_SOURCE_ENV3] = source_values[MOD_MATRIX_SOURCE_ENV3];
            source_valid[MOD_MATRIX_SOURCE_ENV3] = 1U;
        }
        if ((ctx != NULL)
                && (ctx->program_route.mix_track_id < MIXER_MAX_TRACKS)
                && ((required_source_mask
                     & (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_ENV_VCA)) != 0U))
        {
            source_values[MOD_MATRIX_SOURCE_ENV_VCA] = mixer_get_track_vca_env_value(ctx->program_route.mix_track_id);
            source_end[MOD_MATRIX_SOURCE_ENV_VCA] = source_values[MOD_MATRIX_SOURCE_ENV_VCA];
            source_valid[MOD_MATRIX_SOURCE_ENV_VCA] = 1U;
        }
        if ((required_source_mask
             & (uint16_t)(1U << (uint8_t)MOD_MATRIX_SOURCE_ENV_FLT)) != 0U)
        {
            if ((ctx != NULL)
                    && ((ctx->flags & 1U) != 0U)
                    && (ctx->program_route.mix_track_id < MIXER_MAX_TRACKS))
            {
                source_values[MOD_MATRIX_SOURCE_ENV_FLT] =
                    mixer_prepare_track_filter_env_source(
                        ctx->program_route.mix_track_id, elapsed_frames);
                source_end[MOD_MATRIX_SOURCE_ENV_FLT] =
                    source_values[MOD_MATRIX_SOURCE_ENV_FLT];
                source_valid[MOD_MATRIX_SOURCE_ENV_FLT] = 1U;
            }
        }

        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            const track_mod_lfo_state_t *const s = mod_lfo_audio_settings_const(track, lfo);
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
                    || ((required_source_mask
                         & (uint16_t)(1U << ((uint8_t)MOD_MATRIX_SOURCE_LFO1 + lfo))) == 0U))
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

uint8_t mod_lfo_v1_set_track_param_audio(uint8_t track, uint8_t lfo_index,
                                         mod_lfo_param_t param, float value)
{
    if ((lfo_index >= MOD_LFO_COUNT_PER_TRACK)
            || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT)
            || !isfinite(value))
        return 0U;
    uint8_t owner = 0U;
    if (mod_lfo_audio_resolve_owner(track, &owner) == 0U)
        return 0U;
    track = owner;
    track_mod_lfo_state_t *const config =
        &g_mod_lfo_audio_config[track][lfo_index];
    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];
    const mod_lfo_trig_mode_t old_trig = (mod_lfo_trig_mode_t)(uint8_t)(
        mod_lfo_effective_field(rt, config, MOD_LFO_PARAM_TRIG) + 0.5f);
    uint8_t reset = MOD_LFO_SNAPSHOT_RESET_SHAPE;
    rt->temp_valid_mask = (uint8_t)(rt->temp_valid_mask
        & (uint8_t)~mod_lfo_runtime_param_mask(param));
    switch (param)
    {
        case MOD_LFO_PARAM_RATE:
            if ((value < -LFO_FREE_MAX_HZ)
                    || (value > (float)MOD_LFO_SYNC_RATE_COUNT)
                    || ((value > 0.0f) && (value != floorf(value)))) return 0U;
            config->rate = value;
            if ((config->rate <= MOD_LFO_RATE_OFF_EPS)
                    && (config->rate >= -MOD_LFO_RATE_OFF_EPS))
                rt->active = 0U;
            break;
        case MOD_LFO_PARAM_SHAPE:
            if ((value < 0.0f)
                    || (value > (float)((uint8_t)MOD_LFO_SHAPE_COUNT - 1U))
                    || (value != floorf(value))) return 0U;
            config->shape = value;
            break;
        case MOD_LFO_PARAM_TRIG:
            if ((value < 0.0f)
                    || (value > (float)((uint8_t)MOD_LFO_TRIG_COUNT - 1U))
                    || (value != floorf(value))) return 0U;
            config->trig = value;
            reset = MOD_LFO_SNAPSHOT_RESET_TRIGGER;
            break;
        case MOD_LFO_PARAM_PHASE:
            if ((value < 0.0f) || (value > 360.0f)) return 0U;
            config->phase = value;
            break;
        default:
            return 0U;
    }
    if ((reset & MOD_LFO_SNAPSHOT_RESET_SHAPE) != 0U)
    {
        rt->sh_valid = 0U;
        rt->slew_valid = 0U;
        rt->ramp_valid = 0U;
    }
    if ((reset & MOD_LFO_SNAPSHOT_RESET_TRIGGER) != 0U)
    {
        rt->triggered = 0U;
        rt->one_running = 0U;
        rt->one_done = 0U;
        rt->hold_valid = 0U;
        rt->active = 0U;
        rt->ramp_valid = 0U;
        const mod_lfo_trig_mode_t new_trig = (mod_lfo_trig_mode_t)(uint8_t)(
            mod_lfo_effective_field(rt, config, MOD_LFO_PARAM_TRIG) + 0.5f);
        mod_lfo_audio_reset_poly_track_lfo(track, lfo_index,
            (uint8_t)(new_trig >= MOD_LFO_TRIG_POLY_TRIG));
        if (old_trig != new_trig)
            mod_lfo_poly_mode_changed(track, lfo_index, old_trig, new_trig);
    }
    return 1U;
}

uint8_t mod_lfo_v1_apply_track_param_temp(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return 0U;
    }

    uint8_t owner = 0U;
    if (mod_lfo_audio_resolve_owner(track, &owner) == 0U) return 0U;
    track = owner;
    const track_mod_lfo_state_t *const s = mod_lfo_audio_settings_const(track, lfo_index);
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
            if (!isfinite(value) || (value < -LFO_FREE_MAX_HZ)
                    || (value > (float)MOD_LFO_SYNC_RATE_COUNT)
                    || ((value > 0.0f) && (value != floorf(value)))) return 0U;
            rt->temp.rate = value;
            rt->phase_inc = mod_lfo_phase_inc_from_rate(rt->temp.rate);
            if (rt->phase_inc == 0U)
            {
                rt->active = 0U;
            }
            break;

        case MOD_LFO_PARAM_SHAPE:
            if (!isfinite(value) || (value < 0.0f)
                    || (value > (float)((uint8_t)MOD_LFO_SHAPE_COUNT - 1U))
                    || (value != floorf(value))) return 0U;
            rt->temp.shape = value;
            rt->sh_valid = 0U;
            rt->slew_valid = 0U;
            break;

        case MOD_LFO_PARAM_TRIG:
        {
            const mod_lfo_trig_mode_t old_trig = (mod_lfo_trig_mode_t)((uint8_t)
                mod_lfo_effective_field(rt, s, MOD_LFO_PARAM_TRIG));
            if (!isfinite(value) || (value < 0.0f)
                    || (value > (float)((uint8_t)MOD_LFO_TRIG_COUNT - 1U))
                    || (value != floorf(value))) return 0U;
            rt->temp.trig = value;
            rt->triggered = 0U;
            rt->one_running = 0U;
            rt->one_done = 0U;
            rt->hold_valid = 0U;
            rt->active = 0U;
            mod_lfo_poly_mode_changed(track,
                                      lfo_index,
                                      old_trig,
                                      (mod_lfo_trig_mode_t)((uint8_t)rt->temp.trig));
            break;
        }

        case MOD_LFO_PARAM_PHASE:
            if (!isfinite(value) || (value < 0.0f) || (value > 360.0f)) return 0U;
            rt->temp.phase = value;
            rt->slew_valid = 0U;
            break;

        default:
            return 0U;
    }

    rt->temp_valid_mask |= mod_lfo_runtime_param_mask(param);
    return 1U;
}

uint8_t mod_lfo_v1_clear_track_param_temp_audio(uint8_t track,
                                                uint8_t lfo_index,
                                                mod_lfo_param_t param)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return 0U;
    }

    uint8_t owner = 0U;
    if (mod_lfo_audio_resolve_owner(track, &owner) == 0U)
    {
        return 0U;
    }
    track = owner;
    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];
    rt->temp_valid_mask = (uint8_t)(
        rt->temp_valid_mask & (uint8_t)~mod_lfo_runtime_param_mask(param));
    if (param == MOD_LFO_PARAM_TRIG)
    {
        const track_mod_lfo_state_t *const config =
            mod_lfo_audio_settings_const(track, lfo_index);
        if (config == NULL)
        {
            return 0U;
        }
        mod_lfo_audio_reset_poly_track_lfo(
            track, lfo_index,
            (uint8_t)(((uint8_t)config->trig) >= MOD_LFO_TRIG_POLY_TRIG));
    }
    return 1U;
}

mod_lfo_trig_mode_t mod_lfo_v1_effective_trig(uint8_t track, uint8_t lfo_index)
{
    const track_mod_lfo_state_t *const s = mod_lfo_audio_settings_const(track, lfo_index);
    if ((s == NULL) || (track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK))
        return MOD_LFO_TRIG_FREE;
    return (mod_lfo_trig_mode_t)((uint8_t)(mod_lfo_effective_field(
        &g_mod_lfo_runtime[track][lfo_index], s, MOD_LFO_PARAM_TRIG) + 0.5f));
}

void mod_lfo_v1_process_sample_all(void)
{
    mod_lfo_v1_process_block(1U);
}

ITCM_TEXT void mod_lfo_v1_process_block(uint32_t frames)
{
    if (frames == 0U)
    {
        return;
    }

    const uint32_t bpm_milli =
        audio_transport_runtime_get()->tempo_effective_bpm_milli;
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
        const track_mod_lfo_state_t *const s = mod_lfo_audio_settings_const(track, lfo);
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
        const track_mod_lfo_state_t *const s = mod_lfo_audio_settings_const(track, lfo);
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

ITCM_TEXT void mod_lfo_v1_process_poly_voice(uint8_t track,
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
