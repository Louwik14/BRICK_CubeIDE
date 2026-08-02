#include "Param/param_registry_backends.h"

#include "Audio/audio_xfade.h"
#include "Audio/drum_synth.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_deluge_runtime.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/track_tone_sound_state.h"
#include "Audio/md_model.h"
#include "Core/track_sound_state.h"
#include "Core/track_mute.h"
#include "Mod/mod_destination_catalog.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_matrix.h"
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
                     || (id == PARAM_VCA_RELEASE)
                     || (id == PARAM_ENV_RETRIG_VCA));
}

static uint8_t param_backend_prism_param_slot(param_id_t id, uint8_t *out_osc, uint8_t *out_param)
{
    if ((out_osc == NULL) || (out_param == NULL))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_PRISM_EDIT: *out_osc = 0U; *out_param = 0U; return 1U;
        case PARAM_PRISM_FINE: *out_osc = 0U; *out_param = 1U; return 1U;
        case PARAM_PRISM_COARSE: *out_osc = 0U; *out_param = 2U; return 1U;
        case PARAM_PRISM_FM: *out_osc = 0U; *out_param = 3U; return 1U;
        case PARAM_PRISM_TIMBRE: *out_osc = 0U; *out_param = 4U; return 1U;
        case PARAM_PRISM_MODULATION: *out_osc = 0U; *out_param = 5U; return 1U;
        case PARAM_PRISM_COLOR: *out_osc = 0U; *out_param = 6U; return 1U;
        case PARAM_PRISM_PHASE_RESET: *out_osc = 0U; *out_param = 7U; return 1U;
        case PARAM_PRISM_LEVEL: *out_osc = 0U; *out_param = 8U; return 1U;
        case PARAM_PRISM_OSC2_EDIT: *out_osc = 1U; *out_param = 0U; return 1U;
        case PARAM_PRISM_OSC2_FINE: *out_osc = 1U; *out_param = 1U; return 1U;
        case PARAM_PRISM_OSC2_COARSE: *out_osc = 1U; *out_param = 2U; return 1U;
        case PARAM_PRISM_OSC2_FM: *out_osc = 1U; *out_param = 3U; return 1U;
        case PARAM_PRISM_OSC2_TIMBRE: *out_osc = 1U; *out_param = 4U; return 1U;
        case PARAM_PRISM_OSC2_MODULATION: *out_osc = 1U; *out_param = 5U; return 1U;
        case PARAM_PRISM_OSC2_COLOR: *out_osc = 1U; *out_param = 6U; return 1U;
        case PARAM_PRISM_OSC2_PHASE_RESET: *out_osc = 1U; *out_param = 7U; return 1U;
        case PARAM_PRISM_OSC2_LEVEL: *out_osc = 1U; *out_param = 8U; return 1U;
        default: return 0U;
    }
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

uint8_t param_backend_apply_tone_prism(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_PRISM))
    {
        return 0U;
    }

    const uint8_t instance_id = ctx->instance_id;

    uint8_t osc = 0U;
    uint8_t param = 0U;
    if (param_backend_prism_param_slot(id, &osc, &param) == 0U)
    {
        return 0U;
    }

    switch (param)
    {
        case 0U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 38.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->prism.edit[osc] = (float)(uint8_t)(clamped + 0.5f);
            }
            brick6_braids_runtime_set_osc_edit(instance_id, osc, (float)(uint8_t)(clamped + 0.5f));
            return 1U;
        }
        case 1U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->prism.fine[osc] = clamped;
            }
            brick6_braids_runtime_set_osc_fine(instance_id, osc, clamped);
            return 1U;
        }
        case 2U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->prism.coarse[osc] = clamped;
            }
            else
            {
                brick6_braids_runtime_set_osc_fine(instance_id, osc, 0.5f);
            }
            brick6_braids_runtime_set_osc_coarse(instance_id, osc, clamped);
            return 1U;
        }
        case 3U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->prism.fm[osc] = clamped;
            }
            brick6_braids_runtime_set_osc_fm(instance_id, osc, clamped);
            return 1U;
        }
        case 4U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->prism.timbre[osc] = clamped;
            }
            brick6_braids_runtime_set_osc_timbre(instance_id, osc, clamped);
            return 1U;
        }
        case 5U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->prism.modulation[osc] = clamped;
            }
            brick6_braids_runtime_set_osc_modulation(instance_id, osc, clamped);
            return 1U;
        }
        case 6U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->prism.color[osc] = clamped;
            }
            brick6_braids_runtime_set_osc_color(instance_id, osc, clamped);
            return 1U;
        }
        case 7U:
        {
            const float clamped = (value >= 0.5f) ? 1.0f : 0.0f;
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->prism.phase_reset[osc] = clamped;
            }
            brick6_braids_runtime_set_osc_phase_reset(instance_id, osc, (clamped >= 0.5f) ? 1U : 0U);
            return 1U;
        }
        case 8U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->prism.level[osc] = clamped;
            }
            brick6_braids_runtime_set_osc_level(instance_id, osc, clamped);
            return 1U;
        }
        default:
            return 0U;
    }
}

uint8_t param_backend_reapply_tone_prism_runtime(uint8_t track)
{
    const track_tone_sound_state_t *const state = track_tone_sound_state_get_const(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((state == NULL)
            || (ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_PRISM))
    {
        return 0U;
    }

    const uint8_t instance_id = ctx->instance_id;
    for (uint8_t osc = 0U; osc < 2U; ++osc)
    {
        brick6_braids_runtime_set_osc_edit(instance_id, osc, state->prism.edit[osc]);
        brick6_braids_runtime_set_osc_fine(instance_id, osc, state->prism.fine[osc]);
        brick6_braids_runtime_set_osc_coarse(instance_id, osc, state->prism.coarse[osc]);
        brick6_braids_runtime_set_osc_fm(instance_id, osc, state->prism.fm[osc]);
        brick6_braids_runtime_set_osc_timbre(instance_id, osc, state->prism.timbre[osc]);
        brick6_braids_runtime_set_osc_modulation(instance_id, osc, state->prism.modulation[osc]);
        brick6_braids_runtime_set_osc_color(instance_id, osc, state->prism.color[osc]);
        brick6_braids_runtime_set_osc_phase_reset(instance_id, osc, (state->prism.phase_reset[osc] >= 0.5f) ? 1U : 0U);
        brick6_braids_runtime_set_osc_level(instance_id, osc, state->prism.level[osc]);
    }
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
            || (ctx->instance_id >= BRICK6_STACK_VOICE_INSTANCE_COUNT))
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
            || (ctx->instance_id >= BRICK6_STACK_VOICE_INSTANCE_COUNT))
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

static uint8_t param_backend_wave_slot_for_id(param_id_t id, uint8_t *out_osc, uint8_t *out_param)
{
    if ((out_osc == NULL) || (out_param == NULL)
            || (id < PARAM_WAVE_OSC1_TABLE)
            || (id > PARAM_WAVE_OSC2_FLIP))
    {
        return 0U;
    }

    const uint8_t rel = (uint8_t)(id - PARAM_WAVE_OSC1_TABLE);
    *out_osc = (uint8_t)(rel / 8U);
    *out_param = (uint8_t)(rel % 8U);
    return (*out_osc < BRICK6_WAVE_OSC_COUNT) ? 1U : 0U;
}

uint8_t param_backend_apply_tone_wave(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            || (ctx->instance_id >= BRICK6_WAVE_VOICE_INSTANCE_COUNT))
    {
        return 0U;
    }

    if (id == PARAM_WAVE_FRAME_INTERP)
    {
        const uint8_t enabled = (value >= 0.5f) ? 1U : 0U;
        if ((update_base_state != 0U) && (state != NULL))
        {
            state->wave.frame_interp = (float)enabled;
        }
        brick6_wave_runtime_set_frame_interp(ctx->instance_id, enabled);
        return 1U;
    }
    if (id == PARAM_WAVE_SAMPLE_INTERP)
    {
        const uint8_t enabled = (value >= 0.5f) ? 1U : 0U;
        if ((update_base_state != 0U) && (state != NULL))
        {
            state->wave.sample_interp = (float)enabled;
        }
        brick6_wave_runtime_set_sample_interp(ctx->instance_id, enabled);
        return 1U;
    }
    if (id == PARAM_WAVE_POS_UPDATE)
    {
        const brick6_wave_pos_update_t update =
            (brick6_wave_pos_update_t)(uint8_t)(param_backend_clamp_value(value, 0.0f, 3.0f) + 0.5f);
        if ((update_base_state != 0U) && (state != NULL))
        {
            state->wave.pos_update = (float)(uint8_t)update;
        }
        brick6_wave_runtime_set_pos_update(ctx->instance_id, update);
        return 1U;
    }
    if (id == PARAM_WAVE_POS_SMOOTH)
    {
        const uint8_t enabled = (value >= 0.5f) ? 1U : 0U;
        if ((update_base_state != 0U) && (state != NULL))
        {
            state->wave.pos_smooth = (float)enabled;
        }
        brick6_wave_runtime_set_pos_smooth(ctx->instance_id, enabled);
        return 1U;
    }

    uint8_t osc = 0U;
    uint8_t slot_param = 0U;
    if (param_backend_wave_slot_for_id(id, &osc, &slot_param) == 0U)
    {
        return 0U;
    }

    switch (slot_param)
    {
        case 0U:
        {
            const uint16_t global_slot = (uint16_t)(param_backend_clamp_value(value, 0.0f, (float)(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS - 1U)) + 0.5f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.table[osc] = (float)global_slot;
            }
            brick6_wave_runtime_set_osc_table_global(ctx->instance_id, osc, global_slot);
            return 1U;
        }
        case 1U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.pos[osc] = clamped;
            }
            brick6_wave_runtime_set_osc_pos(ctx->instance_id, osc, clamped);
            return 1U;
        }
        case 2U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.start[osc] = clamped;
            }
            brick6_wave_runtime_set_osc_start(ctx->instance_id, osc, clamped);
            return 1U;
        }
        case 3U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.end[osc] = clamped;
            }
            brick6_wave_runtime_set_osc_end(ctx->instance_id, osc, clamped);
            return 1U;
        }
        case 4U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.level[osc] = clamped;
            }
            brick6_wave_runtime_set_osc_level(ctx->instance_id, osc, clamped);
            return 1U;
        }
        case 5U:
        {
            const float clamped = param_backend_clamp_value(value, -60.0f, 60.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.tune[osc] = clamped;
            }
            brick6_wave_runtime_set_osc_tune(ctx->instance_id, osc, clamped);
            return 1U;
        }
        case 6U:
        {
            const brick6_wave_phase_t phase = (brick6_wave_phase_t)(uint8_t)(param_backend_clamp_value(value, 0.0f, 3.0f) + 0.5f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.phase[osc] = (float)(uint8_t)phase;
            }
            brick6_wave_runtime_set_osc_phase(ctx->instance_id, osc, phase);
            return 1U;
        }
        default:
        {
            const brick6_wave_flip_t flip = (brick6_wave_flip_t)(uint8_t)(param_backend_clamp_value(value, 0.0f, 3.0f) + 0.5f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.flip[osc] = (float)(uint8_t)flip;
            }
            brick6_wave_runtime_set_osc_flip(ctx->instance_id, osc, flip);
            return 1U;
        }
    }
}

uint8_t param_backend_reapply_tone_wave_runtime(uint8_t track)
{
    const track_tone_sound_state_t *const state = track_tone_sound_state_get_const(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((state == NULL)
            || (ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            || (ctx->instance_id >= BRICK6_WAVE_VOICE_INSTANCE_COUNT))
    {
        return 0U;
    }

    for (uint8_t osc = 0U; osc < BRICK6_WAVE_OSC_COUNT; ++osc)
    {
        brick6_wave_runtime_set_osc_table_global(ctx->instance_id, osc, (uint16_t)(param_backend_clamp_value(state->wave.table[osc], 0.0f, (float)(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS - 1U)) + 0.5f));
        brick6_wave_runtime_set_osc_pos(ctx->instance_id, osc, param_backend_clamp_value(state->wave.pos[osc], 0.0f, 1.0f));
        brick6_wave_runtime_set_osc_start(ctx->instance_id, osc, param_backend_clamp_value(state->wave.start[osc], 0.0f, 1.0f));
        brick6_wave_runtime_set_osc_end(ctx->instance_id, osc, param_backend_clamp_value(state->wave.end[osc], 0.0f, 1.0f));
        brick6_wave_runtime_set_osc_level(ctx->instance_id, osc, param_backend_clamp_value(state->wave.level[osc], 0.0f, 1.0f));
        brick6_wave_runtime_set_osc_tune(ctx->instance_id, osc, param_backend_clamp_value(state->wave.tune[osc], -60.0f, 60.0f));
        brick6_wave_runtime_set_osc_phase(ctx->instance_id, osc, (brick6_wave_phase_t)(uint8_t)(param_backend_clamp_value(state->wave.phase[osc], 0.0f, 3.0f) + 0.5f));
        brick6_wave_runtime_set_osc_flip(ctx->instance_id, osc, (brick6_wave_flip_t)(uint8_t)(param_backend_clamp_value(state->wave.flip[osc], 0.0f, 3.0f) + 0.5f));
    }
    brick6_wave_runtime_set_frame_interp(ctx->instance_id, (state->wave.frame_interp >= 0.5f) ? 1U : 0U);
    brick6_wave_runtime_set_sample_interp(ctx->instance_id, (state->wave.sample_interp >= 0.5f) ? 1U : 0U);
    brick6_wave_runtime_set_pos_update(ctx->instance_id,
                                       (brick6_wave_pos_update_t)(uint8_t)(param_backend_clamp_value(state->wave.pos_update, 0.0f, 3.0f) + 0.5f));
    brick6_wave_runtime_set_pos_smooth(ctx->instance_id, (state->wave.pos_smooth >= 0.5f) ? 1U : 0U);
    return 1U;
}

uint8_t param_backend_apply_tone_deluge(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_DELUGE)
            || (ctx->instance_id >= BRICK6_DELUGE_VOICE_INSTANCE_COUNT))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_DELUGE_MODEL:
        {
            const brick6_deluge_model_t model =
                (brick6_deluge_model_t)(uint8_t)(param_backend_clamp_value(
                    value, 0.0f, (float)(BRICK6_DELUGE_MODEL_COUNT - 1U)) + 0.5f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                const uint8_t was_square =
                    ((uint8_t)(state->deluge.model + 0.5f)
                     == (uint8_t)BRICK6_DELUGE_MODEL_SQUARE) ? 1U : 0U;
                const uint8_t is_square =
                    (model == BRICK6_DELUGE_MODEL_SQUARE) ? 1U : 0U;
                if ((was_square != 0U) && (is_square == 0U))
                {
                    const float bipolar = (state->deluge.width * 2.0f) - 1.0f;
                    state->deluge.width = (bipolar < 0.0f) ? -bipolar : bipolar;
                }
                else if ((was_square == 0U) && (is_square != 0U))
                {
                    state->deluge.width = 0.5f + (state->deluge.width * 0.5f);
                }
                state->deluge.model = (float)(uint8_t)model;
                mod_matrix_resync_base_on_authoritative_write(
                    track, PARAM_DELUGE_WIDTH, state->deluge.width);
            }
            brick6_deluge_runtime_set_model(ctx->instance_id, model);
            mod_destination_catalog_invalidate_track(track);
            return 1U;
        }
        case PARAM_DELUGE_LEVEL:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL)) { state->deluge.level = clamped; }
            brick6_deluge_runtime_set_level(ctx->instance_id, clamped);
            return 1U;
        }
        case PARAM_DELUGE_TUNE:
        {
            const float clamped = param_backend_clamp_value(value, -48.0f, 48.0f);
            if ((update_base_state != 0U) && (state != NULL)) { state->deluge.tune = clamped; }
            brick6_deluge_runtime_set_tune(ctx->instance_id, clamped);
            return 1U;
        }
        case PARAM_DELUGE_FINE:
        {
            const float clamped = param_backend_clamp_value(value, -100.0f, 100.0f);
            if ((update_base_state != 0U) && (state != NULL)) { state->deluge.fine = clamped; }
            brick6_deluge_runtime_set_fine(ctx->instance_id, clamped);
            return 1U;
        }
        case PARAM_DELUGE_WIDTH:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL)) { state->deluge.width = clamped; }
            brick6_deluge_runtime_set_width(ctx->instance_id, clamped);
            return 1U;
        }
        case PARAM_DELUGE_PHASE:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 360.0f);
            if ((update_base_state != 0U) && (state != NULL)) { state->deluge.phase = clamped; }
            brick6_deluge_runtime_set_phase(ctx->instance_id, clamped);
            return 1U;
        }
        case PARAM_DELUGE_RETRIG:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL)) { state->deluge.retrig = clamped; }
            brick6_deluge_runtime_set_retrig(ctx->instance_id, (clamped >= 0.5f) ? 1U : 0U);
            return 1U;
        }
        default:
            return 0U;
    }
}

uint8_t param_backend_reapply_tone_deluge_runtime(uint8_t track)
{
    const track_tone_sound_state_t *const state = track_tone_sound_state_get_const(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((state == NULL)
            || (ctx == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->engine != (uint8_t)TRACK_RUNTIME_ENGINE_DELUGE)
            || (ctx->instance_id >= BRICK6_DELUGE_VOICE_INSTANCE_COUNT))
    {
        return 0U;
    }

    brick6_deluge_runtime_set_model(
        ctx->instance_id,
        (brick6_deluge_model_t)(uint8_t)(param_backend_clamp_value(
            state->deluge.model, 0.0f, (float)(BRICK6_DELUGE_MODEL_COUNT - 1U)) + 0.5f));
    brick6_deluge_runtime_set_level(ctx->instance_id,
                                    param_backend_clamp_value(state->deluge.level, 0.0f, 1.0f));
    brick6_deluge_runtime_set_tune(ctx->instance_id,
                                   param_backend_clamp_value(state->deluge.tune, -48.0f, 48.0f));
    brick6_deluge_runtime_set_fine(ctx->instance_id,
                                   param_backend_clamp_value(state->deluge.fine, -100.0f, 100.0f));
    brick6_deluge_runtime_set_width(ctx->instance_id,
                                    param_backend_clamp_value(state->deluge.width, 0.0f, 1.0f));
    brick6_deluge_runtime_set_phase(ctx->instance_id,
                                    param_backend_clamp_value(state->deluge.phase, 0.0f, 360.0f));
    brick6_deluge_runtime_set_retrig(ctx->instance_id, (state->deluge.retrig >= 0.5f) ? 1U : 0U);
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

    if ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_EXTERNAL))
    {
        return 1U;
    }

    return 0U;
}

uint8_t param_backend_track_supports_midi_tone_descriptor(const track_runtime_descriptor_t *descriptor)
{
    if (descriptor == NULL)
    {
        return 0U;
    }

    return ((descriptor->family == TRACK_RUNTIME_FAMILY_MIDI)
            || ((descriptor->family == TRACK_RUNTIME_FAMILY_EXTERNAL)
                && (descriptor->type == TRACK_RUNTIME_TYPE_EXTERNAL))) ? 1U : 0U;
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
            if ((ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
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
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
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
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
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
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
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
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
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
            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
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

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
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

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
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

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
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

            if ((ctx == NULL) || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
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
                && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_MD)
                && (id >= PARAM_DRUM_MD_MODEL)
                && (id <= PARAM_DRUM_MD_P8))
        {
            if (id == PARAM_DRUM_MD_MODEL)
            {
                const uint8_t model = md_model_validate(value);
                if ((uint8_t)state->md.model != model)
                {
                    const md_model_profile_t *const profile = md_model_profile_get(model);
                    state->md.model = model;
                    for (uint8_t slot = 0U; slot < 8U; ++slot)
                    {
                        state->md.slot[slot] = profile->defaults[slot];
                    }
                }
            }
            else
            {
                state->md.slot[(uint8_t)(id - PARAM_DRUM_MD_P1)] =
                    (uint8_t)(param_backend_clamp_value(value, 0.0f, 127.0f) + 0.5f);
            }
        }
        if ((state != NULL)
                && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG)
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

/* MacroFX track backend removed with the former fixed FX track. */
#if 0
uint8_t param_backend_apply_macro_fx_track(const track_runtime_ctx_t *ctx,
                                            uint8_t track,
                                            param_id_t id,
                                            float value,
                                            uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const uint8_t slot = (uint8_t)((id - PARAM_MACRO_FX1_TYPE) / 4U);

    if ((ctx == NULL)
            || (state == NULL)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (track_topology_is_role(track, TRACK_TOPOLOGY_ROLE_FX) == 0U)
            || (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SPECIAL_FX)
            || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_SPECIAL_FX)
            || (id < PARAM_MACRO_FX1_TYPE)
            || (id > PARAM_MACRO_FX4_B)
            || (slot >= 4U))
    {
        return 0U;
    }

    if (update_base_state == 0U)
    {
        return 1U;
    }

    switch ((uint8_t)((id - PARAM_MACRO_FX1_TYPE) % 4U))
    {
        case 0U:
            state->macro_fx.type[slot] = param_backend_clamp_value(value, 0.0f, (float)(FX_MASTER_MACRO_TYPE_COUNT - 1U));
            return 1U;
        case 1U:
            state->macro_fx.level[slot] = param_backend_clamp_value(value, 0.0f, 127.0f);
            return 1U;
        case 2U:
            state->macro_fx.macro_a[slot] = param_backend_clamp_value(value, 0.0f, 127.0f);
            return 1U;
        case 3U:
            state->macro_fx.macro_b[slot] = param_backend_clamp_value(value, 0.0f, 127.0f);
            return 1U;
        default:
            return 0U;
    }
}
#endif

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
            && (track_runtime_supports_vca_gate(ctx) == 0U))
    {
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
            return track_mute_apply(track,
                                    (value >= 0.5f) ? 1U : 0U,
                                    update_base_state);

        case PARAM_ENV_RETRIG_FILTER:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            const uint8_t hard = (value >= 0.5f) ? 1U : 0U;
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->env_retrig_filter = (float)hard;
            }

            track_runtime_resolved_track_t resolved;
            if ((track_runtime_resolve_track(track, &resolved) != 0U)
                    && (resolved.has_filter_target != 0U))
            {
                mixer_set_track_filter_retrigger_hard(resolved.filter_track_id, hard);
            }
            return 1U;
        }

        case PARAM_ENV_RETRIG_VCA:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            const uint8_t hard = (value >= 0.5f) ? 1U : 0U;
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->env_retrig_vca = (float)hard;
            }
            mixer_set_track_vca_retrigger_hard(ctx->mix_track_id, hard);
            return 1U;
        }

        case PARAM_ENV_RETRIG_MOD:
            return mod_env3_set_track_retrigger_hard(track, value);

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
            if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            {
                brick6_braids_runtime_set_vca_release_seconds(ctx->instance_id, release_s);
            }
            return 1U;
        }

        default:
            return 0U;
    }
}

uint8_t param_backend_apply_env_track(const track_runtime_ctx_t *ctx, param_id_t id, float value)
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
