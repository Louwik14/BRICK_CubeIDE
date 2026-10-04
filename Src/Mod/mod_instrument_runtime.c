#include "Mod/mod_instrument_runtime.h"

#include <math.h>
#include <string.h>

#include "Audio/audio_transport_runtime.h"
#include "Mod/mod_lfo_segment.h"
#include "Mod/mod_lfo_v1_audio.h"
#include "Mod/mod_matrix.h"
#include "Param/param_filter_audio.h"
#include "Param/param_spec.h"

#define MOD_INSTRUMENT_SAMPLE_RATE 48000.0f

static uint8_t mod_instrument_internal_base(
    const mod_instrument_runtime_t *runtime, param_id_t parameter,
    float *out_value);

static float mod_instrument_clampf(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static uint16_t mod_instrument_seconds_to_u16(float seconds)
{
    const float normalized = mod_instrument_clampf(seconds, 0.0f, 30.0f)
        / 30.0f;
    return (uint16_t)(cbrtf(normalized) * 65535.0f + 0.5f);
}

static uint32_t mod_instrument_phase_from_degrees(float degrees)
{
    degrees = mod_instrument_clampf(degrees, 0.0f, 360.0f);
    if (degrees >= 360.0f) return 0U;
    return (uint32_t)(((double)degrees / 360.0) * 4294967296.0);
}

static uint32_t mod_instrument_rng(uint32_t value)
{
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

static float mod_instrument_random(mod_instrument_lfo_runtime_t *runtime)
{
    runtime->rng = mod_instrument_rng(runtime->rng);
    return ((float)(runtime->rng & 0x00FFFFFFUL)
        * (2.0f / 16777215.0f)) - 1.0f;
}

static void mod_instrument_apply_envelope_settings(
    mod_instrument_runtime_t *runtime, float attack, float decay,
    float sustain, float release)
{
    env_adsr_set_attack(&runtime->envelope, mod_instrument_seconds_to_u16(
        param_filter_audio_attack_s(attack)));
    env_adsr_set_decay(&runtime->envelope, mod_instrument_seconds_to_u16(
        param_filter_audio_decay_s(decay)));
    env_adsr_set_sustain(&runtime->envelope, (uint16_t)(
        mod_instrument_clampf(sustain, 0.0f, 127.0f)
        * (32767.0f / 127.0f)));
    env_adsr_set_release(&runtime->envelope, mod_instrument_seconds_to_u16(
        param_filter_audio_release_s(release)));
}

void mod_instrument_runtime_reset(mod_instrument_runtime_t *runtime)
{
    if (runtime == NULL) return;
    memset(runtime, 0, sizeof(*runtime));
    env_adsr_init(&runtime->envelope, MOD_INSTRUMENT_SAMPLE_RATE);
}

uint8_t mod_instrument_runtime_prepare(mod_instrument_runtime_t *runtime,
    const patch_preview_modulation_t *configuration)
{
    if ((runtime == NULL) || (configuration == NULL)) return 0U;
    mod_instrument_runtime_reset(runtime);
    runtime->config = *configuration;
    for (uint8_t lfo = 0U; lfo < 3U; ++lfo)
    {
        runtime->lfo[lfo].phase = mod_instrument_phase_from_degrees(
            configuration->lfo[lfo].phase_offset);
        runtime->lfo[lfo].rng = 0x6D2B79F5UL
            ^ ((uint32_t)(lfo + 1U) * 0x9E3779B9UL);
        runtime->lfo[lfo].sample_hold = mod_instrument_random(
            &runtime->lfo[lfo]);
        runtime->lfo[lfo].effective_rate = configuration->lfo[lfo].rate;
        runtime->lfo[lfo].active = 1U;
    }
    mod_instrument_apply_envelope_settings(runtime,
        configuration->env_attack, configuration->env_decay,
        configuration->env_sustain, configuration->env_release);
    for (uint8_t route = 0U; route < 8U; ++route)
    {
        const param_id_t parameter = configuration->route[route].destination;
        if ((configuration->route[route].enabled == 0U)
                || (parameter >= PARAM_COUNT)) continue;
        runtime->route_base_valid[route] = mod_instrument_internal_base(
            runtime, parameter, &runtime->route_base[route]);
        if ((runtime->route_base_valid[route] == 0U)
                && (configuration->route[route].base_valid != 0U))
        {
            runtime->route_base[route] = configuration->route[route].base_value;
            runtime->route_base_valid[route] = 1U;
        }
    }
    runtime->prepared = 1U;
    return 1U;
}

void mod_instrument_runtime_note_on(mod_instrument_runtime_t *runtime)
{
    if ((runtime == NULL) || (runtime->prepared == 0U)) return;
    for (uint8_t lfo = 0U; lfo < 3U; ++lfo)
    {
        const uint8_t trigger = runtime->config.lfo[lfo].trigger;
        if (trigger != (uint8_t)MOD_LFO_TRIG_FREE)
        {
            runtime->lfo[lfo].phase = mod_instrument_phase_from_degrees(
                runtime->config.lfo[lfo].phase_offset);
            runtime->lfo[lfo].one_done = 0U;
            runtime->lfo[lfo].active = 1U;
            if ((trigger == (uint8_t)MOD_LFO_TRIG_HOLD)
                    || (runtime->config.lfo[lfo].shape
                        == (uint8_t)MOD_LFO_SHAPE_RANDOM_SH))
                runtime->lfo[lfo].sample_hold = mod_instrument_random(
                    &runtime->lfo[lfo]);
        }
    }
    env_adsr_retrigger(&runtime->envelope,
        runtime->config.env_retrigger_hard != 0U);
}

void mod_instrument_runtime_note_off(mod_instrument_runtime_t *runtime)
{
    if ((runtime == NULL) || (runtime->prepared == 0U)) return;
    env_adsr_gate_off(&runtime->envelope);
}

static float mod_instrument_lfo_process(mod_instrument_runtime_t *runtime,
                                        uint8_t lfo, uint32_t frames)
{
    mod_instrument_lfo_runtime_t *const state = &runtime->lfo[lfo];
    const patch_preview_mod_lfo_t *const config = &runtime->config.lfo[lfo];
    if ((state->active == 0U) || (state->one_done != 0U))
        return mod_lfo_segment_wave(config->shape, 0xFFFFFFFFUL,
                                    state->sample_hold);
    const uint32_t phase_inc = mod_lfo_v1_phase_inc_from_rate(
        state->effective_rate,
        audio_transport_runtime_get()->tempo_effective_bpm_milli);
    if (phase_inc == 0U) return 0.0f;
    const uint32_t before = state->phase;
    const uint32_t after = before
        + (uint32_t)((uint64_t)phase_inc * (uint64_t)frames);
    const uint8_t wrapped = (uint8_t)(after < before);
    if ((wrapped != 0U)
            && (config->shape == (uint8_t)MOD_LFO_SHAPE_RANDOM_SH))
        state->sample_hold = mod_instrument_random(state);
    state->phase = after;
    if ((wrapped != 0U)
            && (config->trigger == (uint8_t)MOD_LFO_TRIG_ONE))
    {
        state->one_done = 1U;
        state->phase = 0xFFFFFFFFUL;
    }
    if (config->trigger == (uint8_t)MOD_LFO_TRIG_HOLD)
        return state->sample_hold;
    return mod_lfo_segment_wave(config->shape, state->phase,
                                state->sample_hold);
}

static uint8_t mod_instrument_source(const float sources[MOD_MATRIX_SOURCE_COUNT],
                                     const uint8_t valid[MOD_MATRIX_SOURCE_COUNT],
                                     uint8_t source, float *out)
{
    if ((out == NULL) || (source >= MOD_MATRIX_SOURCE_COUNT)
            || (valid[source] == 0U)) return 0U;
    *out = sources[source];
    return 1U;
}

static void mod_instrument_process_operators(mod_instrument_runtime_t *runtime,
    float sources[MOD_MATRIX_SOURCE_COUNT],
    uint8_t valid[MOD_MATRIX_SOURCE_COUNT], uint32_t frames)
{
    for (uint8_t op = 0U; op < 2U; ++op)
    {
        float a;
        float b;
        const uint8_t output = (uint8_t)MOD_MATRIX_SOURCE_MULTI1 + op;
        const uint8_t source_a = runtime->config.multi_source[op][0];
        const uint8_t source_b = runtime->config.multi_source[op][1];
        if ((source_a != MOD_MATRIX_SOURCE_MULTI1)
                && (source_a != MOD_MATRIX_SOURCE_MULTI2)
                && (source_b != MOD_MATRIX_SOURCE_MULTI1)
                && (source_b != MOD_MATRIX_SOURCE_MULTI2)
                && mod_instrument_source(sources, valid, source_a, &a)
                && mod_instrument_source(sources, valid, source_b, &b))
        {
            sources[output] = mod_instrument_clampf(a * b, -1.0f, 1.0f);
            valid[output] = 1U;
        }
    }
    for (uint8_t op = 0U; op < 2U; ++op)
    {
        float input;
        const uint8_t output = (uint8_t)MOD_MATRIX_SOURCE_SLEW1 + op;
        const uint8_t source = runtime->config.slew_source[op];
        const uint8_t other = (uint8_t)MOD_MATRIX_SOURCE_SLEW2 - op;
        if ((source == output)
                || ((source == other)
                    && (runtime->config.slew_source[1U - op] == output))
                || !mod_instrument_source(sources, valid, source, &input))
            continue;
        const float amount = mod_instrument_clampf(
            runtime->config.slew_amount[op], 0.0f, 1.0f);
        const float elapsed = (frames == 0U) ? 1.0f : (float)frames;
        const float tau = 16.0f + amount * amount * 48000.0f;
        const float coefficient = (amount <= 0.0f)
            ? 1.0f : elapsed / (tau + elapsed);
        if (runtime->slew_valid[op] == 0U)
        {
            runtime->slew[op] = input;
            runtime->slew_valid[op] = 1U;
        }
        else runtime->slew[op] += (input - runtime->slew[op]) * coefficient;
        sources[output] = mod_instrument_clampf(runtime->slew[op], -1.0f, 1.0f);
        valid[output] = 1U;
    }
}

static uint8_t mod_instrument_internal_base(
    const mod_instrument_runtime_t *runtime, param_id_t parameter,
    float *out_value)
{
    if ((parameter >= PARAM_LFO1_RATE) && (parameter <= PARAM_LFO3_PHASE))
    {
        const uint16_t relative = parameter - PARAM_LFO1_RATE;
        const uint8_t lfo = (uint8_t)(relative / 4U);
        switch (relative % 4U)
        {
            case 0U: *out_value = runtime->config.lfo[lfo].rate; return 1U;
            case 1U: *out_value = (float)runtime->config.lfo[lfo].shape; return 1U;
            case 2U: *out_value = (float)runtime->config.lfo[lfo].trigger; return 1U;
            case 3U: *out_value = runtime->config.lfo[lfo].phase_offset; return 1U;
            default: return 0U;
        }
    }
    switch (parameter)
    {
        case PARAM_ENV3_ATTACK: *out_value = runtime->config.env_attack; return 1U;
        case PARAM_ENV3_DECAY: *out_value = runtime->config.env_decay; return 1U;
        case PARAM_ENV3_SUSTAIN: *out_value = runtime->config.env_sustain; return 1U;
        case PARAM_ENV3_RELEASE: *out_value = runtime->config.env_release; return 1U;
        default: return 0U;
    }
}

static uint8_t mod_instrument_internal_apply(mod_instrument_runtime_t *runtime,
                                              param_id_t parameter,
                                              float value)
{
    if ((parameter >= PARAM_LFO1_RATE) && (parameter <= PARAM_LFO3_PHASE))
    {
        const uint16_t relative = parameter - PARAM_LFO1_RATE;
        const uint8_t lfo = (uint8_t)(relative / 4U);
        if ((relative % 4U) == 0U)
        {
            runtime->lfo[lfo].effective_rate = value;
            return 1U;
        }
        return 0U;
    }
    float attack = runtime->config.env_attack;
    float decay = runtime->config.env_decay;
    float sustain = runtime->config.env_sustain;
    float release = runtime->config.env_release;
    if (parameter == PARAM_ENV3_ATTACK) attack = value;
    else if (parameter == PARAM_ENV3_DECAY) decay = value;
    else if (parameter == PARAM_ENV3_SUSTAIN) sustain = value;
    else if (parameter == PARAM_ENV3_RELEASE) release = value;
    else return 0U;
    mod_instrument_apply_envelope_settings(runtime, attack, decay,
                                           sustain, release);
    return 1U;
}

void mod_instrument_runtime_process(mod_instrument_runtime_t *runtime,
    uint32_t frames, float filter_envelope, float vca_envelope,
    mod_instrument_apply_fn apply, void *context)
{
    if ((runtime == NULL) || (runtime->prepared == 0U) || (frames == 0U))
        return;
    float sources[MOD_MATRIX_SOURCE_COUNT] = {0.0f};
    uint8_t valid[MOD_MATRIX_SOURCE_COUNT] = {0U};
    for (uint8_t lfo = 0U; lfo < 3U; ++lfo)
    {
        const uint8_t source = (uint8_t)MOD_MATRIX_SOURCE_LFO1 + lfo;
        sources[source] = mod_instrument_lfo_process(runtime, lfo, frames);
        valid[source] = 1U;
        runtime->lfo[lfo].effective_rate = runtime->config.lfo[lfo].rate;
    }
    int16_t env_first;
    const int16_t env_value = env_adsr_process_advance(
        &runtime->envelope, frames, &env_first);
    sources[MOD_MATRIX_SOURCE_ENV3] = (float)env_value * (1.0f / 32767.0f);
    sources[MOD_MATRIX_SOURCE_ENV_FLT] = filter_envelope;
    sources[MOD_MATRIX_SOURCE_ENV_VCA] = vca_envelope;
    valid[MOD_MATRIX_SOURCE_ENV3] = 1U;
    valid[MOD_MATRIX_SOURCE_ENV_FLT] = 1U;
    valid[MOD_MATRIX_SOURCE_ENV_VCA] = 1U;
    mod_instrument_process_operators(runtime, sources, valid, frames);

    mod_instrument_destination_t destinations[8U] = {{0}};
    for (uint8_t route = 0U; route < 8U; ++route)
    {
        const patch_preview_mod_route_t *const item = &runtime->config.route[route];
        if ((item->enabled == 0U) || (item->depth == 0.0f)
                || (item->source >= MOD_MATRIX_SOURCE_COUNT)
                || (valid[item->source] == 0U)
                || (item->destination >= PARAM_COUNT)) continue;
        uint8_t destination = 0U;
        while ((destination < 8U) && (destinations[destination].valid != 0U)
                && (destinations[destination].parameter != item->destination))
            ++destination;
        if (destination >= 8U) continue;
        destinations[destination].valid = 1U;
        destinations[destination].parameter = item->destination;
        destinations[destination].sum += sources[item->source]
            * (item->depth / 127.0f)
            * (param_spec[item->destination].max
                - param_spec[item->destination].min);
    }
    for (uint8_t destination = 0U; destination < 8U; ++destination)
    {
        if (destinations[destination].valid == 0U) continue;
        const param_id_t parameter = destinations[destination].parameter;
        float base = 0.0f;
        uint8_t base_valid = mod_instrument_internal_base(
            runtime, parameter, &base);
        if (base_valid == 0U)
        {
            for (uint8_t route = 0U; route < 8U; ++route)
            {
                if ((runtime->config.route[route].enabled != 0U)
                        && (runtime->config.route[route].destination == parameter)
                        && (runtime->route_base_valid[route] != 0U))
                {
                    base = runtime->route_base[route];
                    base_valid = 1U;
                    break;
                }
            }
        }
        if (base_valid == 0U) continue;
        const float value = mod_instrument_clampf(base
            + destinations[destination].sum,
            param_spec[parameter].min, param_spec[parameter].max);
        if (mod_instrument_internal_apply(runtime, parameter, value) == 0U)
        {
            if (apply != NULL) (void)apply(context, parameter, value);
        }
    }
}
