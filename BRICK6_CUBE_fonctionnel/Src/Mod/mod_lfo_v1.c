#include "Mod/mod_lfo_v1.h"

#include <math.h>
#include <string.h>

#include "Core/track_runtime.h"
#include "Param/param_registry.h"

#define MOD_LFO_COUNT_PER_TRACK 2U
#define MOD_LFO_RATE_MAX_HZ 1280.0f
#define MOD_LFO_SAMPLE_RATE 48000.0f
#define MOD_LFO_TWO_PI 6.28318530718f
#define MOD_LFO_DEST_NONE ((param_id_t)PARAM_COUNT)

typedef struct
{
    uint16_t dest;
    uint8_t rate;
    uint8_t depth;
    uint8_t shape;
} mod_lfo_track_settings_t;

typedef struct
{
    float phase;
    uint32_t rng_state;
    float sh_value;
    uint16_t last_dest;
    float base_value;
    uint8_t base_valid;
} mod_lfo_runtime_state_t;

static mod_lfo_track_settings_t g_mod_lfo_settings[SEQ_TRACK_COUNT][MOD_LFO_COUNT_PER_TRACK];
static mod_lfo_runtime_state_t g_mod_lfo_runtime[SEQ_TRACK_COUNT][MOD_LFO_COUNT_PER_TRACK];

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

static uint8_t mod_lfo_dest_supported(uint8_t track, param_id_t dest)
{
    if ((track >= SEQ_TRACK_COUNT) || (dest >= PARAM_COUNT) || (mod_lfo_is_internal_param(dest) != 0U))
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(dest);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_COLORS)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_PLAY))
    {
        return 0U;
    }

    const track_runtime_param_status_t status = track_runtime_get_effective_param_status(track, dest);
    return ((status == TRACK_RUNTIME_PARAM_ALLOWED) || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)) ? 1U : 0U;
}

static float mod_lfo_wave(mod_lfo_shape_t shape, float phase, mod_lfo_runtime_state_t *state)
{
    switch (shape)
    {
        case MOD_LFO_SHAPE_SINE:
            return sinf(phase * MOD_LFO_TWO_PI);

        case MOD_LFO_SHAPE_TRIANGLE:
            return 1.0f - 4.0f * fabsf(phase - 0.5f);

        case MOD_LFO_SHAPE_SAW:
            return (2.0f * phase) - 1.0f;

        case MOD_LFO_SHAPE_SQUARE:
            return (phase < 0.5f) ? 1.0f : -1.0f;

        case MOD_LFO_SHAPE_RANDOM_SH:
            return state->sh_value;

        default:
            return 0.0f;
    }
}

void mod_lfo_v1_init(void)
{
    memset(g_mod_lfo_settings, 0, sizeof(g_mod_lfo_settings));
    memset(g_mod_lfo_runtime, 0, sizeof(g_mod_lfo_runtime));

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            g_mod_lfo_settings[track][lfo].dest = (uint16_t)MOD_LFO_DEST_NONE;
            g_mod_lfo_settings[track][lfo].rate = 0U;
            g_mod_lfo_settings[track][lfo].depth = 0U;
            g_mod_lfo_settings[track][lfo].shape = (uint8_t)MOD_LFO_SHAPE_SINE;

            g_mod_lfo_runtime[track][lfo].phase = 0.0f;
            g_mod_lfo_runtime[track][lfo].rng_state = 0xA341316CU ^ ((uint32_t)track << 8) ^ (uint32_t)lfo;
            g_mod_lfo_runtime[track][lfo].sh_value = 0.0f;
            g_mod_lfo_runtime[track][lfo].last_dest = (uint16_t)MOD_LFO_DEST_NONE;
            g_mod_lfo_runtime[track][lfo].base_valid = 0U;
            g_mod_lfo_runtime[track][lfo].base_value = 0.0f;
        }
    }
}

void mod_lfo_v1_reset_runtime(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            g_mod_lfo_runtime[track][lfo].phase = 0.0f;
            g_mod_lfo_runtime[track][lfo].base_valid = 0U;
            g_mod_lfo_runtime[track][lfo].last_dest = (uint16_t)MOD_LFO_DEST_NONE;
        }
    }
}

uint8_t mod_lfo_v1_set_track_param(uint8_t track, uint8_t lfo_index, mod_lfo_param_t param, float value)
{
    if ((track >= SEQ_TRACK_COUNT) || (lfo_index >= MOD_LFO_COUNT_PER_TRACK) || ((uint8_t)param >= (uint8_t)MOD_LFO_PARAM_COUNT))
    {
        return 0U;
    }

    mod_lfo_track_settings_t *const s = &g_mod_lfo_settings[track][lfo_index];
    mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo_index];

    switch (param)
    {
        case MOD_LFO_PARAM_DEST:
        {
            uint16_t dest = (uint16_t)mod_lfo_clampf(value, 0.0f, (float)PARAM_COUNT);
            s->dest = dest;
            rt->base_valid = 0U;
            rt->last_dest = (uint16_t)MOD_LFO_DEST_NONE;
            return 1U;
        }

        case MOD_LFO_PARAM_RATE:
            s->rate = (uint8_t)mod_lfo_clampf(value, 0.0f, 127.0f);
            return 1U;

        case MOD_LFO_PARAM_DEPTH:
            s->depth = (uint8_t)mod_lfo_clampf(value, 0.0f, 127.0f);
            return 1U;

        case MOD_LFO_PARAM_SHAPE:
            s->shape = (uint8_t)mod_lfo_clampf(value, 0.0f, (float)((uint8_t)MOD_LFO_SHAPE_COUNT - 1U));
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

    const mod_lfo_track_settings_t *const s = &g_mod_lfo_settings[track][lfo_index];

    switch (param)
    {
        case MOD_LFO_PARAM_DEST:
            *out_value = (float)s->dest;
            return 1U;

        case MOD_LFO_PARAM_RATE:
            *out_value = (float)s->rate;
            return 1U;

        case MOD_LFO_PARAM_DEPTH:
            *out_value = (float)s->depth;
            return 1U;

        case MOD_LFO_PARAM_SHAPE:
            *out_value = (float)s->shape;
            return 1U;

        default:
            return 0U;
    }
}

void mod_lfo_v1_process_sample_all(void)
{
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            mod_lfo_track_settings_t *const s = &g_mod_lfo_settings[track][lfo];
            mod_lfo_runtime_state_t *const rt = &g_mod_lfo_runtime[track][lfo];
            const param_id_t dest = (param_id_t)s->dest;

            if ((dest == MOD_LFO_DEST_NONE) || (s->depth == 0U))
            {
                continue;
            }

            if (mod_lfo_dest_supported(track, dest) == 0U)
            {
                continue;
            }

            if ((rt->base_valid == 0U) || (rt->last_dest != s->dest))
            {
                if (param_registry_get_track_value(dest, track, &rt->base_value) == 0U)
                {
                    continue;
                }
                rt->base_valid = 1U;
                rt->last_dest = s->dest;
            }

            const float hz = ((float)s->rate / 127.0f) * MOD_LFO_RATE_MAX_HZ;
            const float phase_inc = hz / MOD_LFO_SAMPLE_RATE;
            const float old_phase = rt->phase;
            float phase = old_phase + phase_inc;
            uint8_t wrapped = 0U;
            if (phase >= 1.0f)
            {
                phase -= floorf(phase);
                wrapped = 1U;
            }
            rt->phase = phase;

            if (((mod_lfo_shape_t)s->shape == MOD_LFO_SHAPE_RANDOM_SH) && (wrapped != 0U))
            {
                rt->rng_state = (rt->rng_state * 1664525U) + 1013904223U;
                const uint32_t u = (rt->rng_state >> 8) & 0x00FFFFFFU;
                rt->sh_value = ((float)u / 8388607.5f) - 1.0f;
            }

            const float w = mod_lfo_wave((mod_lfo_shape_t)s->shape, old_phase, rt);
            const param_desc_t *const desc = &param_registry[dest];
            const float span = desc->max - desc->min;
            const float depth = ((float)s->depth / 127.0f) * span;
            const float modulated = mod_lfo_clampf(rt->base_value + (w * depth), desc->min, desc->max);
            (void)param_registry_apply_track_value(dest, track, modulated);
        }
    }
}
