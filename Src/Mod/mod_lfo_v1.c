#include "Mod/mod_lfo_v1.h"

#include <math.h>
#include <string.h>

#include "Core/track_sound_state.h"
#include "Core/track_runtime.h"
#include "Param/param_registry.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "ui_core.h"

#define MOD_LFO_COUNT_PER_TRACK 2U
#define MOD_LFO_RATE_STEP_COUNT 15U
#define MOD_LFO_AUDIO_SAMPLE_RATE 48000.0f
#define MOD_LFO_CONTROL_RATE_HZ 3000.0f
#define MOD_LFO_CONTROL_STRIDE ((uint32_t)(MOD_LFO_AUDIO_SAMPLE_RATE / MOD_LFO_CONTROL_RATE_HZ))
#define MOD_LFO_CONTROL_DT (1.0f / MOD_LFO_CONTROL_RATE_HZ)
#define MOD_LFO_DEST_NONE ((param_id_t)PARAM_COUNT)
#define MOD_LFO_SINE_LUT_SIZE 256U

/* Musical rate table: bars per cycle (4/4), bounded to 1/128. */
static const float g_mod_lfo_rate_bars_per_cycle[MOD_LFO_RATE_STEP_COUNT] = {
    128.0f, 64.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f,
    0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f, 0.015625f, 0.0078125f
};

static const float g_mod_lfo_sine_lut[MOD_LFO_SINE_LUT_SIZE + 1U] = {
#include "mod_lfo_sine_lut_257.inc"
};

typedef struct
{
    uint32_t phase;
    uint32_t phase_inc;
    float current;
    uint32_t rng_state;
    float sh_value;
    uint16_t last_dest;
    float base_value;
    float dest_min;
    float dest_max;
    float depth_scale;
    uint8_t base_valid;
    uint8_t calib_valid;
} mod_lfo_runtime_state_t;

typedef struct
{
    uint8_t valid;
    uint8_t ui_family;
    uint8_t ui_type;
    uint8_t rt_bind_state;
    uint8_t rt_family;
    uint8_t rt_type;
    uint8_t rt_mix_track_id;
    uint16_t count;
    param_id_t index_to_param[PARAM_COUNT + 1U];
    uint16_t param_to_index[PARAM_COUNT];
} mod_lfo_dest_cache_t;

static mod_lfo_runtime_state_t g_mod_lfo_runtime[SEQ_TRACK_COUNT][MOD_LFO_COUNT_PER_TRACK];
static mod_lfo_dest_cache_t g_mod_lfo_dest_cache[SEQ_TRACK_COUNT];
static uint32_t g_mod_lfo_control_counter = 0U;

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

static param_id_t mod_lfo_track_settings_dest(uint8_t track, uint8_t lfo_index)
{
    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    if (s == NULL)
    {
        return MOD_LFO_DEST_NONE;
    }

    return (param_id_t)((uint16_t)(s->dest + 0.5f));
}

static uint8_t mod_lfo_is_internal_param(param_id_t id)
{
    switch (id)
    {
        case PARAM_LFO1_DEST:
        case PARAM_LFO1_RATE:
        case PARAM_LFO1_DEPTH:
        case PARAM_LFO1_SHAPE:
        case PARAM_LFO2_DEST:
        case PARAM_LFO2_RATE:
        case PARAM_LFO2_DEPTH:
        case PARAM_LFO2_SHAPE:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t mod_lfo_param_matches_track_context(ui_track_family_t family,
                                                   ui_track_type_t type,
                                                   param_id_t dest,
                                                   track_runtime_param_domain_t domain)
{
    if (domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
    {
        if (family == UI_TRACK_FAMILY_MIDI)
        {
            return ((dest >= PARAM_MIDI_CC1_1) && (dest <= PARAM_MIDI_CC3_4)) ? 1U : 0U;
        }

        if (family == UI_TRACK_FAMILY_DRUM)
        {
            switch (type)
            {
                case UI_TRACK_TYPE_DRUM_TRX_BD:
                    return ((dest >= PARAM_DRUM_TRX_BD_PITCH) && (dest <= PARAM_DRUM_TRX_BD_DRIVE)) ? 1U : 0U;
                case UI_TRACK_TYPE_DRUM_TRX_CLAVES:
                    return ((dest >= PARAM_DRUM_TRX_CLAVES_PITCH) && (dest <= PARAM_DRUM_TRX_CLAVES_DRIVE)) ? 1U : 0U;
                case UI_TRACK_TYPE_DRUM_TRX_HIHAT:
                    return ((dest >= PARAM_DRUM_TRX_HIHAT_DECAY) && (dest <= PARAM_DRUM_TRX_HIHAT_PEAK)) ? 1U : 0U;
                case UI_TRACK_TYPE_DRUM_TRX_SNARE:
                    return ((dest >= PARAM_DRUM_TRX_SNARE_PITCH) && (dest <= PARAM_DRUM_TRX_SNARE_BUMP)) ? 1U : 0U;
                case UI_TRACK_TYPE_DRUM_FM_KICK:
                    return ((dest >= PARAM_DRUM_FM_KICK_PITCH) && (dest <= PARAM_DRUM_FM_KICK_MOD_ENV_SYNC)) ? 1U : 0U;
                case UI_TRACK_TYPE_DRUM_FM_SNARE:
                    return ((dest >= PARAM_DRUM_FM_SNARE_PITCH) && (dest <= PARAM_DRUM_FM_SNARE_NOISE_DECAY)) ? 1U : 0U;
                case UI_TRACK_TYPE_DRUM_FM_TOM:
                    return ((dest >= PARAM_DRUM_FM_TOM_PITCH) && (dest <= PARAM_DRUM_FM_TOM_START_PHASE)) ? 1U : 0U;
                case UI_TRACK_TYPE_DRUM_FM_RIMSHOT:
                    return ((dest >= PARAM_DRUM_FM_RIMSHOT_RIM_PITCH) && (dest <= PARAM_DRUM_FM_RIMSHOT_MOD_DECAY)) ? 1U : 0U;
                case UI_TRACK_TYPE_DRUM_FM_CLAP:
                    return ((dest >= PARAM_DRUM_FM_CLAP_CLAP_COUNT) && (dest <= PARAM_DRUM_FM_CLAP_CLAP_DECAY)) ? 1U : 0U;
                case UI_TRACK_TYPE_DRUM_FM_COWBELL:
                    return ((dest >= PARAM_DRUM_FM_COWBELL_PITCH) && (dest <= PARAM_DRUM_FM_COWBELL_MOD_FREQ)) ? 1U : 0U;
                case UI_TRACK_TYPE_DRUM_FM_CYMBAL:
                    return ((dest >= PARAM_DRUM_FM_CYMBAL_DECAY) && (dest <= PARAM_DRUM_FM_CYMBAL_MOD_DECAY)) ? 1U : 0U;
                default:
                    return 0U;
            }
        }

        if ((ui_track_family_is_engine(family) == 0) || (type == UI_TRACK_TYPE_AUDIO) || (type == UI_TRACK_TYPE_HYBRID))
        {
            return 0U;
        }

        return 0U;
    }

    if (domain == TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
    {
        if (family == UI_TRACK_FAMILY_MIDI)
        {
            return 0U;
        }

        return ((dest >= PARAM_FILTER_TYPE) && (dest <= PARAM_FILTER_DRIVE)) ? 1U : 0U;
    }

    return 0U;
}

static track_runtime_param_status_t mod_lfo_effective_status_from_ctx(const track_runtime_ctx_t *ctx,
                                                                      track_runtime_resource_t resource)
{
    if (ctx == NULL)
    {
        return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }

    switch (resource)
    {
        case TRACK_RUNTIME_RESOURCE_NONE:
            return TRACK_RUNTIME_PARAM_ALLOWED;

        case TRACK_RUNTIME_RESOURCE_FILTER:
            if ((ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_OFF))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return TRACK_RUNTIME_PARAM_ALLOWED;

        case TRACK_RUNTIME_RESOURCE_SYNTH:
            if ((ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                    || ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                        && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return TRACK_RUNTIME_PARAM_ALLOWED;

        case TRACK_RUNTIME_RESOURCE_PLAY:
            return ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI))
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        default:
            return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }
}

static uint8_t mod_lfo_dest_supported_fast(uint8_t track,
                                           param_id_t dest,
                                           ui_track_family_t family,
                                           ui_track_type_t type,
                                           const track_runtime_ctx_t *ctx)
{
    if ((track >= SEQ_TRACK_COUNT) || (dest >= PARAM_COUNT) || (mod_lfo_is_internal_param(dest) != 0U))
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(dest);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE))
    {
        return 0U;
    }

    if (rule.resource == TRACK_RUNTIME_RESOURCE_BUFFER)
    {
        return 0U;
    }

    if (mod_lfo_param_matches_track_context(family, type, dest, rule.domain) == 0U)
    {
        return 0U;
    }

    const track_runtime_param_status_t status = mod_lfo_effective_status_from_ctx(ctx, rule.resource);
    return ((status == TRACK_RUNTIME_PARAM_ALLOWED) || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

static void mod_lfo_dest_cache_invalidate_track_internal(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_mod_lfo_dest_cache[track].valid = 0U;
}

void mod_lfo_v1_invalidate_dest_cache_track(uint8_t track)
{
    mod_lfo_dest_cache_invalidate_track_internal(track);
}

void mod_lfo_v1_invalidate_dest_cache_all(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        mod_lfo_dest_cache_invalidate_track_internal(track);
    }
}

static uint8_t mod_lfo_dest_cache_matches_context(const mod_lfo_dest_cache_t *cache,
                                                  ui_track_family_t family,
                                                  ui_track_type_t type,
                                                  const track_runtime_ctx_t *ctx)
{
    if ((cache == NULL) || (cache->valid == 0U))
    {
        return 0U;
    }

    const uint8_t ctx_bind_state = (ctx != NULL) ? ctx->bind_state : 0xFFU;
    const uint8_t ctx_family = (ctx != NULL) ? ctx->family : 0xFFU;
    const uint8_t ctx_type = (ctx != NULL) ? ctx->type : 0xFFU;
    const uint8_t ctx_mix_track_id = (ctx != NULL) ? ctx->mix_track_id : 0xFFU;

    return ((cache->ui_family == (uint8_t)family)
            && (cache->ui_type == (uint8_t)type)
            && (cache->rt_bind_state == ctx_bind_state)
            && (cache->rt_family == ctx_family)
            && (cache->rt_type == ctx_type)
            && (cache->rt_mix_track_id == ctx_mix_track_id))
            ? 1U
            : 0U;
}

static mod_lfo_dest_cache_t *mod_lfo_dest_cache_resolve(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return NULL;
    }

    track_runtime_refresh_track(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    const ui_track_family_t family = ui_get_track_family(track);
    const ui_track_type_t type = ui_get_track_type(track);
    mod_lfo_dest_cache_t *const cache = &g_mod_lfo_dest_cache[track];

    if (mod_lfo_dest_cache_matches_context(cache, family, type, ctx) != 0U)
    {
        return cache;
    }

    cache->index_to_param[0] = MOD_LFO_DEST_NONE;
    for (uint16_t raw = 0U; raw < (uint16_t)PARAM_COUNT; ++raw)
    {
        cache->param_to_index[raw] = 0U;
    }

    uint16_t count = 1U;
    for (uint16_t raw = 0U; raw < (uint16_t)PARAM_COUNT; ++raw)
    {
        const param_id_t param = (param_id_t)raw;
        if (mod_lfo_dest_supported_fast(track, param, family, type, ctx) == 0U)
        {
            continue;
        }

        if (count <= (uint16_t)PARAM_COUNT)
        {
            cache->index_to_param[count] = param;
            cache->param_to_index[raw] = count;
        }
        ++count;
    }

    cache->count = count;
    cache->ui_family = (uint8_t)family;
    cache->ui_type = (uint8_t)type;
    cache->rt_bind_state = (ctx != NULL) ? ctx->bind_state : 0xFFU;
    cache->rt_family = (ctx != NULL) ? ctx->family : 0xFFU;
    cache->rt_type = (ctx != NULL) ? ctx->type : 0xFFU;
    cache->rt_mix_track_id = (ctx != NULL) ? ctx->mix_track_id : 0xFFU;
    cache->valid = 1U;

    return cache;
}

static uint16_t mod_lfo_dest_count_supported(uint8_t track)
{
    mod_lfo_dest_cache_t *const cache = mod_lfo_dest_cache_resolve(track);
    return (cache != NULL) ? cache->count : 1U;
}

static param_id_t mod_lfo_dest_from_index(uint8_t track, uint16_t dest_index)
{
    if ((track >= SEQ_TRACK_COUNT) || (dest_index == 0U))
    {
        return MOD_LFO_DEST_NONE;
    }

    mod_lfo_dest_cache_t *const cache = mod_lfo_dest_cache_resolve(track);
    if ((cache == NULL) || (dest_index >= cache->count))
    {
        return MOD_LFO_DEST_NONE;
    }

    return cache->index_to_param[dest_index];
}

static uint16_t mod_lfo_dest_to_index(uint8_t track, param_id_t dest)
{
    if ((track >= SEQ_TRACK_COUNT) || (dest >= PARAM_COUNT))
    {
        return 0U;
    }

    mod_lfo_dest_cache_t *const cache = mod_lfo_dest_cache_resolve(track);
    if (cache == NULL)
    {
        return 0U;
    }

    return cache->param_to_index[(uint16_t)dest];
}

static uint32_t mod_lfo_phase_inc_from_rate_with_bpm(uint8_t rate_index, uint32_t bpm_milli)
{
    const uint8_t idx = (rate_index < MOD_LFO_RATE_STEP_COUNT) ? rate_index : (MOD_LFO_RATE_STEP_COUNT - 1U);
    const float bpm = (float)bpm_milli * 0.001f;
    const float bars_per_cycle = g_mod_lfo_rate_bars_per_cycle[idx];
    const float seconds_per_cycle = bars_per_cycle * (240.0f / mod_lfo_clampf(bpm, 40.0f, 300.0f));
    const float hz = 1.0f / mod_lfo_clampf(seconds_per_cycle, 0.0005f, 60.0f);
    const double phase_f = (double)hz * (4294967296.0 * (double)MOD_LFO_CONTROL_DT);
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

static uint32_t mod_lfo_phase_inc_from_rate(uint8_t rate_index)
{
    return mod_lfo_phase_inc_from_rate_with_bpm(rate_index, seq_runtime_get_tempo_bpm_milli());
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
    switch (shape)
    {
        case MOD_LFO_SHAPE_SINE:
        {
            const uint32_t lut_pos = phase >> 24;
            const uint32_t frac = (phase >> 8) & 0xFFFFU;
            const float y0 = g_mod_lfo_sine_lut[lut_pos];
            const float y1 = g_mod_lfo_sine_lut[lut_pos + 1U];
            return y0 + (y1 - y0) * ((float)frac * (1.0f / 65535.0f));
        }

        case MOD_LFO_SHAPE_TRIANGLE:
        {
            const float p = (float)phase * (1.0f / 4294967296.0f);
            return 1.0f - 4.0f * fabsf(p - 0.5f);
        }

        case MOD_LFO_SHAPE_SAW:
            return ((float)phase * (2.0f / 4294967296.0f)) - 1.0f;

        case MOD_LFO_SHAPE_SQUARE:
            return (phase < 0x80000000U) ? 1.0f : -1.0f;

        case MOD_LFO_SHAPE_RANDOM_SH:
            return state->sh_value;

        default:
            return 0.0f;
    }
}

static uint8_t mod_lfo_is_effectively_active(uint8_t track,
                                              uint8_t lfo_index,
                                              ui_track_family_t family,
                                              ui_track_type_t type,
                                              const track_runtime_ctx_t *ctx)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK))
    {
        return 0U;
    }

    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    const param_id_t dest = mod_lfo_track_settings_dest(track, lfo_index);
    if ((s == NULL) || (dest == MOD_LFO_DEST_NONE) || (s->depth == 0.0f))
    {
        return 0U;
    }

    return mod_lfo_dest_supported_fast(track, dest, family, type, ctx);
}

static void mod_lfo_release_last_destination(uint8_t track,
                                             uint8_t lfo_index,
                                             ui_track_family_t family,
                                             ui_track_type_t type,
                                             const track_runtime_ctx_t *ctx)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK))
    {
        return;
    }

    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];
    const param_id_t previous_dest = (param_id_t)rt->last_dest;

    if ((previous_dest >= PARAM_COUNT) || (rt->base_valid == 0U))
    {
        rt->base_valid = 0U;
        rt->last_dest = (uint16_t)MOD_LFO_DEST_NONE;
        rt->calib_valid = 0U;
        rt->depth_scale = 0.0f;
        return;
    }

    uint8_t other_active_same_dest = 0U;
    for (uint8_t other = 0U; other < MOD_LFO_COUNT_PER_TRACK; ++other)
    {
        if (other == lfo_index)
        {
            continue;
        }
        if (mod_lfo_is_effectively_active(track, other, family, type, ctx) == 0U)
        {
            continue;
        }
        if (mod_lfo_track_settings_dest(track, other) == previous_dest)
        {
            other_active_same_dest = 1U;
            break;
        }
    }

    if ((other_active_same_dest == 0U)
            && (mod_lfo_dest_supported_fast(track, previous_dest, family, type, ctx) != 0U))
    {
        (void)param_registry_apply_track_value_rt_fast(previous_dest, track, rt->base_value);
    }

    rt->base_valid = 0U;
    rt->last_dest = (uint16_t)MOD_LFO_DEST_NONE;
    rt->calib_valid = 0U;
    rt->depth_scale = 0.0f;
}

static void mod_lfo_process_control_tick(void)
{
    if (param_registry_track_structure_transition_is_active() != 0U)
    {
        return;
    }

    const uint32_t bpm_milli = seq_runtime_get_tempo_bpm_milli();
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_runtime_refresh_track(track);
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        const ui_track_family_t family = ui_get_track_family(track);
        const ui_track_type_t type = ui_get_track_type(track);

        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            track_mod_lfo_state_t *const s = mod_lfo_track_settings_mut(track, lfo);
            mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo];
            const param_id_t dest = mod_lfo_track_settings_dest(track, lfo);

            if (s == NULL)
            {
                continue;
            }

            if ((rt->last_dest != (uint16_t)MOD_LFO_DEST_NONE)
                    && (rt->last_dest != (uint16_t)(s->dest + 0.5f)))
            {
                mod_lfo_release_last_destination(track, lfo, family, type, ctx);
            }

            if ((dest == MOD_LFO_DEST_NONE) || (s->depth == 0.0f)
                    || (mod_lfo_dest_supported_fast(track, dest, family, type, ctx) == 0U))
            {
                mod_lfo_release_last_destination(track, lfo, family, type, ctx);
                continue;
            }

            if ((rt->base_valid == 0U) || (rt->last_dest != (uint16_t)(s->dest + 0.5f)))
            {
                /* Query seam: seed the modulation base from the pure value surface only. */
                if (param_registry_get_track_value(dest, track, &rt->base_value) == 0U)
                {
                    continue;
                }
                rt->last_dest = (uint16_t)(s->dest + 0.5f);
                rt->base_valid = 1U;
            }

            rt->phase_inc = mod_lfo_phase_inc_from_rate_with_bpm((uint8_t)(s->rate + 0.5f), bpm_milli);
            const uint32_t phase_prev = rt->phase;
            rt->phase += rt->phase_inc;

            if (((mod_lfo_shape_t)((uint8_t)(s->shape + 0.5f)) == MOD_LFO_SHAPE_RANDOM_SH) && (rt->phase < phase_prev))
            {
                rt->sh_value = mod_lfo_sh_next_value(&rt->rng_state);
            }

            rt->current = mod_lfo_wave((mod_lfo_shape_t)((uint8_t)(s->shape + 0.5f)), phase_prev, rt);
            if ((rt->calib_valid == 0U) || (rt->last_dest != (uint16_t)(s->dest + 0.5f)))
            {
                const param_desc_t *const desc = &param_registry[dest];
                rt->dest_min = desc->min;
                rt->dest_max = desc->max;
                rt->calib_valid = 1U;
            }
            rt->depth_scale = (s->depth / 127.0f) * (rt->dest_max - rt->dest_min);
            const float modulated = mod_lfo_clampf(rt->base_value + (rt->current * rt->depth_scale), rt->dest_min, rt->dest_max);
            /* RT apply seam: modulation writes use the fast track-aware mutation path. */
            (void)param_registry_apply_track_value_rt_fast(dest, track, modulated);
        }
    }
}

void mod_lfo_v1_init(void)
{
    memset(g_mod_lfo_runtime, 0, sizeof(g_mod_lfo_runtime));
    memset(g_mod_lfo_dest_cache, 0, sizeof(g_mod_lfo_dest_cache));
    g_mod_lfo_control_counter = 0U;

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            g_mod_lfo_runtime[track][lfo].phase = 0U;
            g_mod_lfo_runtime[track][lfo].phase_inc = 1U;
            g_mod_lfo_runtime[track][lfo].current = 0.0f;
            g_mod_lfo_runtime[track][lfo].rng_state = 0xA341316CU ^ ((uint32_t)track << 8) ^ (uint32_t)lfo;
            g_mod_lfo_runtime[track][lfo].sh_value = 0.0f;
            g_mod_lfo_runtime[track][lfo].last_dest = (uint16_t)MOD_LFO_DEST_NONE;
            g_mod_lfo_runtime[track][lfo].base_valid = 0U;
            g_mod_lfo_runtime[track][lfo].base_value = 0.0f;
            g_mod_lfo_runtime[track][lfo].dest_min = 0.0f;
            g_mod_lfo_runtime[track][lfo].dest_max = 127.0f;
            g_mod_lfo_runtime[track][lfo].depth_scale = 0.0f;
            g_mod_lfo_runtime[track][lfo].calib_valid = 0U;
        }
    }

    mod_lfo_v1_invalidate_dest_cache_all();
}

void mod_lfo_v1_reset_runtime(void)
{
    g_mod_lfo_control_counter = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            g_mod_lfo_runtime[track][lfo].phase = 0U;
            {
                const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo);
                const uint8_t rate = (s != NULL) ? (uint8_t)(s->rate + 0.5f) : 7U;
                g_mod_lfo_runtime[track][lfo].phase_inc = mod_lfo_phase_inc_from_rate(rate);
            }
            g_mod_lfo_runtime[track][lfo].current = 0.0f;
            g_mod_lfo_runtime[track][lfo].base_valid = 0U;
            g_mod_lfo_runtime[track][lfo].last_dest = (uint16_t)MOD_LFO_DEST_NONE;
            g_mod_lfo_runtime[track][lfo].depth_scale = 0.0f;
            g_mod_lfo_runtime[track][lfo].calib_valid = 0U;
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

    track_mod_lfo_state_t *const s = mod_lfo_track_settings_mut(track, lfo_index);
    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];

    if (s == NULL)
    {
        return 0U;
    }

    switch (param)
    {
        case MOD_LFO_PARAM_DEST:
        {
            track_runtime_refresh_track(track);
            mod_lfo_release_last_destination(track, lfo_index, ui_get_track_family(track), ui_get_track_type(track), track_runtime_get_ctx(track));
            const uint16_t max_index = (uint16_t)(mod_lfo_dest_count_supported(track) - 1U);
            const uint16_t dest_index = (uint16_t)mod_lfo_clampf(value, 0.0f, (float)max_index);
            s->dest = (float)mod_lfo_dest_from_index(track, dest_index);
            rt->base_valid = 0U;
            rt->last_dest = (uint16_t)MOD_LFO_DEST_NONE;
            rt->calib_valid = 0U;
            rt->depth_scale = 0.0f;
            return 1U;
        }

        case MOD_LFO_PARAM_RATE:
            s->rate = mod_lfo_clampf(value, 0.0f, (float)(MOD_LFO_RATE_STEP_COUNT - 1U));
            rt->phase_inc = mod_lfo_phase_inc_from_rate((uint8_t)(s->rate + 0.5f));
            return 1U;

        case MOD_LFO_PARAM_DEPTH:
            s->depth = mod_lfo_clampf(value, 0.0f, 127.0f);
            if (s->depth == 0.0f)
            {
                track_runtime_refresh_track(track);
                mod_lfo_release_last_destination(track, lfo_index, ui_get_track_family(track), ui_get_track_type(track), track_runtime_get_ctx(track));
            }
            return 1U;

        case MOD_LFO_PARAM_SHAPE:
            s->shape = mod_lfo_clampf(value, 0.0f, (float)((uint8_t)MOD_LFO_SHAPE_COUNT - 1U));
            return 1U;

        default:
            return 0U;
    }
}

uint8_t mod_lfo_v1_get_track_param(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float *out_value)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || (out_value == NULL)
            || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return 0U;
    }

    const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo_index);
    if (s == NULL)
    {
        return 0U;
    }

    switch (param)
    {
        case MOD_LFO_PARAM_DEST:
            *out_value = (float)mod_lfo_dest_to_index(track, (param_id_t)((uint16_t)(s->dest + 0.5f)));
            return 1U;

        case MOD_LFO_PARAM_RATE:
            *out_value = s->rate;
            return 1U;

        case MOD_LFO_PARAM_DEPTH:
            *out_value = s->depth;
            return 1U;

        case MOD_LFO_PARAM_SHAPE:
            *out_value = s->shape;
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

    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        const track_mod_lfo_state_t *const s = mod_lfo_track_settings_const(track, lfo);
        mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo];
        if ((s == NULL) || ((param_id_t)((uint16_t)(s->dest + 0.5f)) != id))
        {
            continue;
        }
        if (((param_id_t)rt->last_dest != id) || (rt->base_valid == 0U))
        {
            continue;
        }

        rt->base_value = value;
    }
}

void mod_lfo_v1_process_sample_all(void)
{
    mod_lfo_v1_process_block(1U);
}

void mod_lfo_v1_process_block(uint32_t frames)
{
    if (frames == 0U)
    {
        return;
    }

    g_mod_lfo_control_counter += frames;
    while (g_mod_lfo_control_counter >= MOD_LFO_CONTROL_STRIDE)
    {
        g_mod_lfo_control_counter -= MOD_LFO_CONTROL_STRIDE;
        mod_lfo_process_control_tick();
    }
}

uint16_t mod_lfo_v1_dest_count(uint8_t track)
{
    return mod_lfo_dest_count_supported(track);
}

uint8_t mod_lfo_v1_dest_param_at(uint8_t track, uint16_t dest_index, param_id_t *out_param)
{
    if (out_param == NULL)
    {
        return 0U;
    }

    *out_param = mod_lfo_dest_from_index(track, dest_index);
    return 1U;
}

uint8_t mod_lfo_v1_dest_label(uint8_t track, uint16_t dest_index, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return 0U;
    }

    const param_id_t dest = mod_lfo_dest_from_index(track, dest_index);
    if (dest == MOD_LFO_DEST_NONE)
    {
        out[0] = 'N';
        out[1] = 'o';
        out[2] = 'n';
        out[3] = 'e';
        out[4] = '\0';
        return 1U;
    }

    if (dest >= PARAM_COUNT)
    {
        return 0U;
    }

    const char *name = param_registry[dest].name;
    if (name == NULL)
    {
        return 0U;
    }

    uint32_t i = 0U;
    for (; (i + 1U) < out_len; ++i)
    {
        const char c = name[i];
        out[i] = c;
        if (c == '\0')
        {
            return 1U;
        }
    }
    out[i] = '\0';
    return 1U;
}
