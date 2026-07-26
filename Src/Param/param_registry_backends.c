#include "Param/param_registry_backends.h"

#include "Audio/fx_master_macro.h"
#include "Audio/audio_xfade.h"
#include "Audio/drum_synth.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/track_tone_sound_state.h"
#include "Core/track_sound_state.h"
#include "Param/param_filter.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_pool.h"
#include "midi.h"
#include "mixer.h"

static float param_backend_clamp_value(float v, float lo, float hi)
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

static uint8_t param_backend_is_vca_param(param_id_t id)
{
    return (uint8_t)((id == PARAM_VCA_ATTACK)
                     || (id == PARAM_VCA_DECAY)
                     || (id == PARAM_VCA_SUSTAIN)
                     || (id == PARAM_VCA_RELEASE));
}

static uint8_t param_backend_ctx_is_sampler_clip_or_looper(const track_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return 0U;
    }

    return (uint8_t)(((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                      && ((ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_CLIP)
                          || (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))) ? 1U : 0U);
}

static uint8_t param_backend_clip_size_index(float value)
{
    const uint8_t index = (uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f);
    return (index <= 5U) ? index : 5U;
}

static uint16_t param_backend_clip_size_value(uint8_t index)
{
    static const uint16_t values[] = {32U, 64U, 96U, 128U, 256U, 512U};
    return values[(index <= 5U) ? index : 5U];
}

static uint16_t param_backend_clip_grain_size_value(uint8_t index)
{
    static const uint16_t values[] = {384U, 512U, 768U, 1024U, 1536U, 2048U};
    return values[(index <= 5U) ? index : 5U];
}

static uint16_t param_backend_multi_instrument_from_selector(float value)
{
    if (value < 0.5f)
    {
        return MULTI_SAMPLE_POOL_INVALID_ID;
    }

    const uint8_t selector = (uint8_t)(param_backend_clamp_value(value,
                                                                 0.0f,
                                                                 (float)MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
                                      + 0.5f);
    uint8_t current = 1U;
    uint16_t last_instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;

    for (uint16_t id = 0U; id < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++id)
    {
        if (multi_sample_pool_get_instrument(id) == NULL)
        {
            continue;
        }

        last_instrument_id = id;
        if (current == selector)
        {
            return id;
        }
        current++;
    }

    return last_instrument_id;
}

static uint8_t param_backend_stream_backend_from_global_selector(float value,
                                                                uint16_t *out_global_slot,
                                                                uint16_t *out_stream_slot)
{
    const uint16_t active_slots = sample_global_pool_get_active_slot_capacity();
    const float max_slot = (active_slots > 0U) ? (float)(active_slots - 1U) : 0.0f;
    const uint16_t global_slot =
        (uint16_t)(param_backend_clamp_value(value, 0.0f, max_slot) + 0.5f);
    uint16_t stream_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;

    if (out_global_slot != NULL)
    {
        *out_global_slot = global_slot;
    }
    if (out_stream_slot != NULL)
    {
        *out_stream_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }

    const sample_global_slot_t *const slot = sample_global_pool_get_slot(global_slot);
    if ((slot == NULL)
        || (slot->kind != SAMPLE_GLOBAL_KIND_STREAM)
        || (slot->state != SAMPLE_GLOBAL_STATE_READY)
        || (sample_global_pool_resolve_backend(global_slot,
                                               SAMPLE_GLOBAL_KIND_STREAM,
                                               &stream_slot) == 0U)
        || (stream_slot >= SAMPLE_POOL_SIZE)
        || (sample_pool_is_loaded(stream_slot) == 0U))
    {
        return 0U;
    }

    if (out_stream_slot != NULL)
    {
        *out_stream_slot = stream_slot;
    }
    return 1U;
}

static uint16_t param_backend_global_slot_from_selector(float value)
{
    const uint16_t active_slots = sample_global_pool_get_active_slot_capacity();
    const float max_slot = (active_slots > 0U) ? (float)(active_slots - 1U) : 0.0f;
    return (uint16_t)(param_backend_clamp_value(value, 0.0f, max_slot) + 0.5f);
}

static void param_backend_project_looper_stretch(uint8_t track,
                                                 const track_tone_sound_state_t *state,
                                                 param_id_t override_id,
                                                 float override_value)
{
    if (state == NULL)
    {
        return;
    }

    float stretch = state->looper.stretch;
    float pitch = state->looper.pitch;
    float grain = state->looper.grain;
    if (override_id == PARAM_LOOPER_STRETCH)
    {
        stretch = override_value;
    }
    else if (override_id == PARAM_LOOPER_PITCH)
    {
        pitch = override_value;
    }
    else if (override_id == PARAM_LOOPER_GRAIN)
    {
        grain = override_value;
    }

    const uint8_t mode = (uint8_t)(param_backend_clamp_value(stretch, 0.0f, 2.0f) + 0.5f);
    const uint8_t grain_index = (uint8_t)(param_backend_clamp_value(grain, 0.0f, 5.0f) + 0.5f);
    brick6_looper_runtime_set_stretch(track,
                                      mode,
                                      param_backend_clamp_value(pitch, -12.0f, 12.0f),
                                      param_backend_clip_grain_size_value(grain_index));
}

static uint8_t param_backend_clip_search_index(float value)
{
    const uint8_t index = (uint8_t)(param_backend_clamp_value(value, 0.0f, 4.0f) + 0.5f);
    return (index <= 4U) ? index : 4U;
}

uint8_t param_backend_apply_tone_wave(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE))
    {
        return 0U;
    }

    const uint8_t instance_id = ctx->instance_id;

    switch (id)
    {
        case PARAM_WAVE_EDIT:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 47.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.edit = (float)(uint8_t)(clamped + 0.5f);
            }
            brick6_braids_runtime_set_edit(instance_id, (float)(uint8_t)(clamped + 0.5f));
            return 1U;
        }
        case PARAM_WAVE_FINE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.fine = clamped;
            }
            brick6_braids_runtime_set_fine(instance_id, clamped);
            return 1U;
        }
        case PARAM_WAVE_COARSE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.coarse = clamped;
            }
            brick6_braids_runtime_set_coarse(instance_id, clamped);
            return 1U;
        }
        case PARAM_WAVE_FM:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.fm = clamped;
            }
            brick6_braids_runtime_set_fm(instance_id, clamped);
            return 1U;
        }
        case PARAM_WAVE_TIMBRE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.timbre = clamped;
            }
            brick6_braids_runtime_set_timbre(instance_id, clamped);
            return 1U;
        }
        case PARAM_WAVE_MODULATION:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.modulation = clamped;
            }
            brick6_braids_runtime_set_modulation(instance_id, clamped);
            return 1U;
        }
        case PARAM_WAVE_COLOR:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.color = clamped;
            }
            brick6_braids_runtime_set_color(instance_id, clamped);
            return 1U;
        }
        case PARAM_WAVE_PHASE_RESET:
        {
            const float clamped = (value >= 0.5f) ? 1.0f : 0.0f;
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.phase_reset = clamped;
            }
            brick6_braids_runtime_set_phase_reset(instance_id, (clamped >= 0.5f) ? 1U : 0U);
            return 1U;
        }
        default:
            return 0U;
    }
}

uint8_t param_backend_reapply_tone_wave_runtime(uint8_t track)
{
    const track_tone_sound_state_t *const state = track_tone_sound_state_get_const(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((state == NULL)
            || (ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE))
    {
        return 0U;
    }

    const uint8_t instance_id = ctx->instance_id;
    brick6_braids_runtime_set_edit(instance_id, state->wave.edit);
    brick6_braids_runtime_set_fine(instance_id, state->wave.fine);
    brick6_braids_runtime_set_coarse(instance_id, state->wave.coarse);
    brick6_braids_runtime_set_fm(instance_id, state->wave.fm);
    brick6_braids_runtime_set_timbre(instance_id, state->wave.timbre);
    brick6_braids_runtime_set_modulation(instance_id, state->wave.modulation);
    brick6_braids_runtime_set_color(instance_id, state->wave.color);
    brick6_braids_runtime_set_phase_reset(instance_id, (state->wave.phase_reset >= 0.5f) ? 1U : 0U);
    return 1U;
}

static uint8_t param_backend_stack_slot_for_id(param_id_t id, uint8_t *out_slot, uint8_t *out_param)
{
    if ((out_slot == NULL) || (out_param == NULL))
    {
        return 0U;
    }

    if ((id >= PARAM_STACK_OSC1_LEVEL) && (id <= PARAM_STACK_OSC3_LEVEL))
    {
        *out_slot = (uint8_t)(id - PARAM_STACK_OSC1_LEVEL);
        *out_param = 0U;
        return 1U;
    }
    if ((id >= PARAM_STACK_OSC1_MODEL) && (id <= PARAM_STACK_OSC3_PARAM3))
    {
        const uint8_t rel = (uint8_t)(id - PARAM_STACK_OSC1_MODEL);
        *out_slot = (uint8_t)(rel / 5U);
        *out_param = (uint8_t)((rel % 5U) + 1U);
        return (*out_slot < BRICK6_STACK_SLOT_COUNT) ? 1U : 0U;
    }

    return 0U;
}

uint8_t param_backend_apply_tone_stack(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            || (ctx->instance_id >= BRICK6_STACK_MAX_INSTANCES))
    {
        return 0U;
    }

    if (id == PARAM_STACK_NOISE_LEVEL)
    {
        const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
        if ((update_base_state != 0U) && (state != NULL))
        {
            state->stack.noise_level = clamped;
        }
        if (update_base_state == 0U)
        {
            brick6_stack_runtime_set_noise_level(ctx->instance_id, clamped);
            return 1U;
        }
        return brick6_stack_runtime_submit_noise_level(ctx->instance_id, clamped);
    }
    if (id == PARAM_STACK_OSC_DETUNE)
    {
        const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
        if ((update_base_state != 0U) && (state != NULL))
        {
            state->stack.osc_detune = clamped;
        }
        if (update_base_state == 0U)
        {
            brick6_stack_runtime_set_osc_detune(ctx->instance_id, clamped);
            return 1U;
        }
        return brick6_stack_runtime_submit_osc_detune(ctx->instance_id, clamped);
    }
    if (id == PARAM_STACK_PHASE_RESET)
    {
        const uint8_t enabled = (value >= 0.5f) ? 1U : 0U;
        if ((update_base_state != 0U) && (state != NULL))
        {
            state->stack.phase_reset = (float)enabled;
        }
        if (update_base_state == 0U)
        {
            brick6_stack_runtime_set_phase_reset(ctx->instance_id, enabled);
            return 1U;
        }
        return brick6_stack_runtime_submit_phase_reset(ctx->instance_id, enabled);
    }

    uint8_t slot = 0U;
    uint8_t slot_param = 0U;
    if (param_backend_stack_slot_for_id(id, &slot, &slot_param) == 0U)
    {
        return 0U;
    }

    switch (slot_param)
    {
        case 0U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->stack.level[slot] = clamped;
            }
            if (update_base_state == 0U)
            {
                brick6_stack_runtime_set_slot_level(ctx->instance_id, slot, clamped);
                return 1U;
            }
            return brick6_stack_runtime_submit_slot_level(ctx->instance_id, slot, clamped);
        }
        case 1U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, (float)(BRICK6_STACK_MODEL_COUNT - 1U));
            const brick6_stack_model_t model = (brick6_stack_model_t)(uint8_t)(clamped + 0.5f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->stack.model[slot] = (float)(uint8_t)model;
            }
            if (update_base_state == 0U)
            {
                brick6_stack_runtime_set_slot_model(ctx->instance_id, slot, model);
                return 1U;
            }
            return brick6_stack_runtime_submit_slot_model(ctx->instance_id, slot, model);
        }
        case 2U:
        {
            const float clamped = param_backend_clamp_value(value, -24.0f, 24.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->stack.tune[slot] = clamped;
            }
            if (update_base_state == 0U)
            {
                brick6_stack_runtime_set_slot_tune(ctx->instance_id, slot, clamped);
                return 1U;
            }
            return brick6_stack_runtime_submit_slot_tune(ctx->instance_id, slot, clamped);
        }
        case 3U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->stack.timbre[slot] = clamped;
            }
            if (update_base_state == 0U)
            {
                brick6_stack_runtime_set_slot_timbre(ctx->instance_id, slot, clamped);
                return 1U;
            }
            return brick6_stack_runtime_submit_slot_timbre(ctx->instance_id, slot, clamped);
        }
        case 4U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->stack.color[slot] = clamped;
            }
            if (update_base_state == 0U)
            {
                brick6_stack_runtime_set_slot_color(ctx->instance_id, slot, clamped);
                return 1U;
            }
            return brick6_stack_runtime_submit_slot_color(ctx->instance_id, slot, clamped);
        }
        default:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->stack.param3[slot] = clamped;
            }
            if (update_base_state == 0U)
            {
                brick6_stack_runtime_set_slot_param3(ctx->instance_id, slot, clamped);
                return 1U;
            }
            return brick6_stack_runtime_submit_slot_param3(ctx->instance_id, slot, clamped);
        }
    }
}

uint8_t param_backend_reapply_tone_stack_runtime(uint8_t track)
{
    const track_tone_sound_state_t *const state = track_tone_sound_state_get_const(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((state == NULL)
            || (ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            || (ctx->instance_id >= BRICK6_STACK_MAX_INSTANCES))
    {
        return 0U;
    }

    for (uint8_t slot = 0U; slot < BRICK6_STACK_SLOT_COUNT; ++slot)
    {
        (void)brick6_stack_runtime_submit_slot_level(ctx->instance_id, slot, state->stack.level[slot]);
        (void)brick6_stack_runtime_submit_slot_model(ctx->instance_id,
                                                     slot,
                                                     (brick6_stack_model_t)(uint8_t)(param_backend_clamp_value(state->stack.model[slot],
                                                                                                               0.0f,
                                                                                                               (float)(BRICK6_STACK_MODEL_COUNT - 1U)) + 0.5f));
        (void)brick6_stack_runtime_submit_slot_tune(ctx->instance_id,
                                                    slot,
                                                    param_backend_clamp_value(state->stack.tune[slot], -24.0f, 24.0f));
        (void)brick6_stack_runtime_submit_slot_timbre(ctx->instance_id, slot, state->stack.timbre[slot]);
        (void)brick6_stack_runtime_submit_slot_color(ctx->instance_id, slot, state->stack.color[slot]);
        (void)brick6_stack_runtime_submit_slot_param3(ctx->instance_id, slot, state->stack.param3[slot]);
    }
    (void)brick6_stack_runtime_submit_noise_level(ctx->instance_id, state->stack.noise_level);
    (void)brick6_stack_runtime_submit_osc_detune(ctx->instance_id, state->stack.osc_detune);
    (void)brick6_stack_runtime_submit_phase_reset(ctx->instance_id, (state->stack.phase_reset >= 0.5f) ? 1U : 0U);
    return 1U;
}

uint8_t param_backend_is_midi_cc_id(param_id_t id)
{
    return ((id >= PARAM_MIDI_CC1_1) && (id <= PARAM_MIDI_CC3_4)) ? 1U : 0U;
}

uint8_t param_backend_midi_cc_number_from_id(param_id_t id)
{
    if (param_backend_is_midi_cc_id(id) == 0U)
    {
        return 0U;
    }

    return (uint8_t)(16U + (uint8_t)(id - PARAM_MIDI_CC1_1));
}

uint8_t param_backend_track_supports_midi_tone_ctx(const track_runtime_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    if (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI)
    {
        return 1U;
    }

    return ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_HYBRID)) ? 1U : 0U;
}

uint8_t param_backend_track_supports_midi_tone_descriptor(const track_runtime_descriptor_t *descriptor)
{
    if (descriptor == NULL)
    {
        return 0U;
    }

    return ((descriptor->family == TRACK_RUNTIME_FAMILY_MIDI)
            || ((descriptor->family == TRACK_RUNTIME_FAMILY_INPUT)
                && (descriptor->type == TRACK_RUNTIME_TYPE_HYBRID))) ? 1U : 0U;
}

uint8_t param_backend_send_midi_cc(uint8_t track, param_id_t id, float value)
{
    if (param_backend_is_midi_cc_id(id) == 0U)
    {
        return 0U;
    }

    {
        const uint8_t cc_number = param_backend_midi_cc_number_from_id(id);
        const uint8_t cc_value = (uint8_t)(param_backend_clamp_value(value, 0.0f, 127.0f) + 0.5f);
        const uint8_t channel = track_runtime_get_midi_channel_zero_based(track);

        midi_cc(MIDI_DEST_BOTH, channel, cc_number, cc_value);
    }

    return 1U;
}

uint8_t param_backend_apply_tone_sampler(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);

    switch (id)
    {
        case PARAM_SAMPLER_SAMPLE:
            if ((ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))
            {
                brick6_sampler_runtime_set_multi_instrument(track,
                                                            param_backend_multi_instrument_from_selector(value));
                return 1U;
            }
        {
            uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
            uint16_t stream_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
            if ((ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                if (param_backend_stream_backend_from_global_selector(value,
                                                                      &global_slot,
                                                                      &stream_slot) == 0U)
                {
                    brick6_sampler_runtime_stop(track);
                    return 0U;
                }
                if ((update_base_state != 0U) && (state != NULL))
                {
                    state->sample = (float)global_slot;
                }
                brick6_sampler_runtime_set_sample(track, stream_slot);
                return 1U;
            }

            global_slot = param_backend_global_slot_from_selector(value);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->sample = (float)global_slot;
            }
            brick6_sampler_runtime_set_sample(track, global_slot);
            return 1U;
        }
        case PARAM_SAMPLER_GAIN:
            if ((ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))
            {
                const float gain = param_backend_clamp_value(value, 0.0f, 2.0f);
                if ((update_base_state != 0U) && (state != NULL))
                {
                    state->gain = gain;
                }
                brick6_sampler_runtime_set_multi_gain(track, gain);
                return 1U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->gain = param_backend_clamp_value(value, 0.0f, 2.0f);
            }
            brick6_sampler_runtime_set_gain(track, param_backend_clamp_value(value, 0.0f, 2.0f));
            return 1U;
        case PARAM_SAMPLER_START:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->start = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_start(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_END:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->end = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_end(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_MODE:
        {
            uint8_t mode = (uint8_t)(param_backend_clamp_value(value, 0.0f, 5.0f) + 0.5f);
            if (mode >= 4U)
            {
                mode = 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mode = (float)mode;
            }
            brick6_sampler_runtime_set_mode(track, mode);
            return 1U;
        }
        case PARAM_SAMPLER_TUNE:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->tune = param_backend_clamp_value(value, -24.0f, 24.0f);
            }
            brick6_sampler_runtime_set_tune(track, param_backend_clamp_value(value, -24.0f, 24.0f));
            return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
        {
            static const uint8_t counts[] = {0U, 2U, 4U, 8U, 16U, 32U, 64U};
            const uint8_t idx = (uint8_t)(param_backend_clamp_value(value, 0.0f, 6.0f) + 0.5f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->slice_count = (float)idx;
            }
            brick6_sampler_runtime_set_slice_count(track, counts[idx]);
            return 1U;
        }
        case PARAM_SAMPLER_LOOP_START:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->loop_start = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_loop_start(track, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.source_bpm = param_backend_clamp_value(value, 40.0f, 300.0f);
            }
            brick6_sampler_runtime_set_clip_source_bpm(track, param_backend_clamp_value(value, 40.0f, 300.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.sync_length = param_backend_clamp_value(value, 0.0f, 4.0f);
            }
            brick6_sampler_runtime_set_clip_sync_length(track,
                                                        (uint8_t)(param_backend_clamp_value(value, 0.0f, 4.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_PITCH:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.pitch = param_backend_clamp_value(value, -12.0f, 12.0f);
            }
            brick6_sampler_runtime_set_clip_pitch(track, param_backend_clamp_value(value, -12.0f, 12.0f));
            return 1U;
        case PARAM_SAMPLER_CLIP_PLAY_MODE:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.play_mode = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_clip_play_mode(track,
                                                      (uint8_t)(param_backend_clamp_value(value, 0.0f, 1.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_LOOP:
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.loop = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            brick6_sampler_runtime_set_clip_loop(track,
                                                 (uint8_t)(param_backend_clamp_value(value, 0.0f, 1.0f) + 0.5f));
            return 1U;
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
        {
            const uint8_t stretch_mode = (uint8_t)(param_backend_clamp_value(value, 0.0f, 2.0f) + 0.5f);

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }

            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.stretch_mode = (float)stretch_mode;
            }
            brick6_sampler_runtime_set_clip_stretch_mode(track, stretch_mode);
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_GRAIN:
        {
            uint8_t grain_index;

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }

            grain_index = param_backend_clip_size_index(value);

            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.grain_size = (float)grain_index;
            }

            brick6_sampler_runtime_set_clip_grain_size(track, param_backend_clip_grain_size_value(grain_index));
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_HOP:
        {
            uint8_t grain_index;
            uint8_t hop_index;

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }

            grain_index = (state != NULL) ? param_backend_clip_size_index(state->clip.grain_size) : 4U;
            hop_index = param_backend_clip_size_index(value);
            if (param_backend_clip_size_value(hop_index) > param_backend_clip_size_value(grain_index))
            {
                hop_index = grain_index;
            }

            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.grain_size = (float)grain_index;
                state->clip.hop_size = (float)hop_index;
            }
            return 1U;
        }
        case PARAM_SAMPLER_CLIP_SEARCH:
        {
            const uint8_t search_index = param_backend_clip_search_index(value);

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_CLIP))
            {
                return 0U;
            }

            if ((update_base_state != 0U) && (state != NULL))
            {
                state->clip.search_size = (float)search_index;
            }
            return 1U;
        }
        case PARAM_SAMPLER_MULTI_LOOP:
        {
            const uint8_t enabled =
                (param_backend_clamp_value(value, 0.0f, 1.0f) >= 0.5f) ? 1U : 0U;
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_MULTI))
            {
                return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->multi.loop = enabled != 0U ? 1.0f : 0.0f;
            }
            brick6_sampler_runtime_set_multi_loop(track, enabled);
            return 1U;
        }
        default:
            return 0U;
    }
}

uint8_t param_backend_apply_tone_looper(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);

    if ((ctx == NULL)
            || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_LOOPER_ARM:
            if ((update_base_state == 0U) || (state == NULL))
            {
                return 1U;
            }
            state->looper.arm = param_backend_clamp_value(value, 0.0f, 2.0f);
            return 1U;
        case PARAM_LOOPER_LEN:
            if ((update_base_state == 0U) || (state == NULL))
            {
                return 1U;
            }
            state->looper.len = param_backend_clamp_value(value, 0.0f, 5.0f);
            return 1U;
        case PARAM_LOOPER_PLAY:
            if ((update_base_state == 0U) || (state == NULL))
            {
                return 1U;
            }
            state->looper.play = param_backend_clamp_value(value, 0.0f, 1.0f);
            brick6_looper_runtime_set_play_auto(track, (state->looper.play >= 0.5f) ? 1U : 0U);
            return 1U;
        case PARAM_LOOPER_XFADE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->looper.xfade = clamped;
            }
            audio_xfade_set(clamped);
            return 1U;
        }
        case PARAM_LOOPER_STRETCH:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->looper.stretch = param_backend_clamp_value(value, 0.0f, 2.0f);
            }
            param_backend_project_looper_stretch(track, state, id, value);
            return 1U;
        case PARAM_LOOPER_PITCH:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->looper.pitch = param_backend_clamp_value(value, -12.0f, 12.0f);
            }
            param_backend_project_looper_stretch(track, state, id, value);
            return 1U;
        case PARAM_LOOPER_GRAIN:
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->looper.grain = param_backend_clamp_value(value, 0.0f, 5.0f);
            }
            param_backend_project_looper_stretch(track, state, id, value);
            return 1U;
        default:
            return 0U;
    }
}

uint8_t param_backend_apply_tone_drum(uint8_t track,
                                      const track_runtime_ctx_t *ctx,
                                      param_id_t id,
                                      float value,
                                      uint8_t update_base_state)
{
    if (ctx == NULL)
    {
        return 0U;
    }

    if (update_base_state != 0U)
    {
        track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
        if ((state != NULL)
                && ((ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_TRX_BD)
                    || (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG))
                && (id >= PARAM_DRUM_TRX_BD_PITCH)
                && (id <= PARAM_DRUM_TRX_BD_DRIVE))
        {
            switch (id)
            {
                case PARAM_DRUM_TRX_BD_PITCH:
                    state->trx_bd.pitch = value;
                    break;
                case PARAM_DRUM_TRX_BD_DECAY:
                    state->trx_bd.decay = value;
                    break;
                case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
                    state->trx_bd.pitch_sweep = value;
                    break;
                case PARAM_DRUM_TRX_BD_SWEEP_DECAY:
                    state->trx_bd.sweep_decay = value;
                    break;
                case PARAM_DRUM_TRX_BD_ATTACK:
                    state->trx_bd.attack = value;
                    break;
                case PARAM_DRUM_TRX_BD_NOISE:
                    state->trx_bd.noise = value;
                    break;
                case PARAM_DRUM_TRX_BD_HARMONICS:
                    state->trx_bd.harmonics = value;
                    break;
                case PARAM_DRUM_TRX_BD_DRIVE:
                    state->trx_bd.drive = value;
                    break;
                default:
                    break;
            }
        }
    }

    return drum_synth_set_param_for_instance(ctx->instance_id, id, value);
}

uint8_t param_backend_apply_master_fx_track(const track_runtime_ctx_t *ctx,
                                            uint8_t track,
                                            param_id_t id,
                                            float value,
                                            uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const uint8_t slot = (uint8_t)((id - PARAM_MASTER_FX1_TYPE) / 4U);

    if ((ctx == NULL)
            || (state == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_MASTER)
            || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_MASTER_FX)
            || (id < PARAM_MASTER_FX1_TYPE)
            || (id > PARAM_MASTER_FX4_B)
            || (slot >= 4U))
    {
        return 0U;
    }

    if (update_base_state == 0U)
    {
        return 1U;
    }

    switch ((uint8_t)((id - PARAM_MASTER_FX1_TYPE) % 4U))
    {
        case 0U:
            state->master_fx.type[slot] = param_backend_clamp_value(value, 0.0f, (float)(FX_MASTER_MACRO_TYPE_COUNT - 1U));
            return 1U;
        case 1U:
            state->master_fx.level[slot] = param_backend_clamp_value(value, 0.0f, 127.0f);
            return 1U;
        case 2U:
            state->master_fx.macro_a[slot] = param_backend_clamp_value(value, 0.0f, 127.0f);
            return 1U;
        case 3U:
            state->master_fx.macro_b[slot] = param_backend_clamp_value(value, 0.0f, 127.0f);
            return 1U;
        default:
            return 0U;
    }
}

uint8_t param_backend_apply_mix_track(const track_runtime_ctx_t *ctx,
                                      uint8_t track,
                                      param_id_t id,
                                      float value,
                                      uint8_t update_base_state)
{
    if ((ctx == NULL) || (track_runtime_is_audio_routable(track) == 0U))
    {
        return 0U;
    }

    if ((param_backend_is_vca_param(id) != 0U)
            && (param_backend_ctx_is_sampler_clip_or_looper(ctx) != 0U))
    {
        mixer_track_vca_all_notes_off((uint32_t)ctx->mix_track_id);
        mixer_set_track_vca_enabled((uint32_t)ctx->mix_track_id, 0U);
        return 0U;
    }

    switch (id)
    {
        case PARAM_MIX_LEVEL:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_level = param_backend_clamp_value(value, 0.0f, 2.0f);
            }
            mixer_set_track_gain(ctx->mix_track_id, param_backend_clamp_value(value, 0.0f, 2.0f));
            return 1U;
        }

        case PARAM_MIX_PAN:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_pan = param_backend_clamp_value(value, -1.0f, 1.0f);
            }
            mixer_set_track_pan(ctx->mix_track_id, param_backend_clamp_value(value, -1.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND1:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_send1 = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            mixer_set_track_send_level(ctx->mix_track_id, 0U, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND2:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_send2 = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            mixer_set_track_send_level(ctx->mix_track_id, 1U, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_MUTE:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_mute = (value >= 0.5f) ? 1.0f : 0.0f;
            }
            mixer_set_track_mute(ctx->mix_track_id, (value >= 0.5f) ? 1U : 0U);
            return 1U;
        }

        case PARAM_HYBRID_GATE:
            if ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
                    || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_HYBRID))
            {
                return 0U;
            }
            {
                track_sound_state_t *state = track_sound_state_get(track);
                if ((update_base_state != 0U) && (state != NULL))
                {
                    state->input.hybrid_gate = (value >= 0.5f) ? 1.0f : 0.0f;
                }
            }
            mixer_set_track_vca_enabled(ctx->mix_track_id, (value >= 0.5f) ? 1U : 0U);
            if (value < 0.5f)
            {
                mixer_track_vca_all_notes_off((uint32_t)ctx->mix_track_id);
            }
            return 1U;

        case PARAM_VCA_ATTACK:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->vca_attack = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_attack(ctx->mix_track_id, param_filter_ui127_to_attack_s(value));
            return 1U;
        }

        case PARAM_VCA_DECAY:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->vca_decay = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_decay(ctx->mix_track_id, param_filter_ui127_to_decay_s(value));
            return 1U;
        }

        case PARAM_VCA_SUSTAIN:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->vca_sustain = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_sustain(ctx->mix_track_id, param_filter_ui127_to_sustain(value));
            return 1U;
        }

        case PARAM_VCA_RELEASE:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            const float release_s = param_filter_ui127_to_release_s(value);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->vca_release = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_release(ctx->mix_track_id, release_s);
            if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            {
                brick6_braids_runtime_set_vca_release_seconds(ctx->instance_id, release_s);
            }
            return 1U;
        }

        default:
            return 0U;
    }
}

uint8_t param_backend_apply_colors_track(const track_runtime_ctx_t *ctx, param_id_t id, float value)
{
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    switch (ctx->engine)
    {
        case (uint8_t)TRACK_RUNTIME_ENGINE_DRUM:
            return drum_synth_set_param_for_instance(ctx->instance_id, id, value);
        default:
            return 0U;
    }
}
