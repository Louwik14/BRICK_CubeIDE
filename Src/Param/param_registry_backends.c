#include "Param/param_registry_backends.h"
#include "Audio/audio_note_engine_adapter.h"

#include "Audio/audio_xfade.h"
#include "Audio/drum_synth.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_fm_runtime.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/project_control.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/audio_wave_table_projection.h"
#include "Core/track_tone_sound_state.h"
#include "Audio/md_model.h"
#include "Core/track_sound_state.h"
#include "Audio/vca_env.h"
#include "Core/track_mute.h"
#include "Mod/mod_destination_catalog.h"
#include "Mod/mod_matrix.h"
#include "Param/param_filter.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_pool.h"
#include "midi.h"
#include "mixer.h"
#include <math.h>

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

static void param_backend_fm_store_frequency(track_tone_fm_operator_base_t *op, float value)
{
    if (op->mode == 0U)
    {
        float best_error = 1.0e30f;
        for (uint8_t coarse = 0U; coarse < 32U; ++coarse)
        {
            const float base = (coarse == 0U) ? 0.5f : (float)coarse;
            int fine = (int)(((value / base) - 1.0f) * 100.0f + 0.5f);
            if (fine < 0) fine = 0;
            if (fine > 99) fine = 99;
            const float represented = base * (1.0f + 0.01f * (float)fine);
            const float error = fabsf(represented - value);
            if (error < best_error)
            {
                best_error = error;
                op->coarse = coarse;
                op->fine = (uint8_t)fine;
            }
        }
    }
    else
    {
        float hz = 440.0f * value;
        if (hz < 1.0f) hz = 1.0f;
        int code = (int)(log10f(hz) * 100.0f + 0.5f);
        if (code < 0) code = 0;
        if (code > 399) code = 399;
        op->coarse = (uint8_t)(code / 100);
        op->fine = (uint8_t)(code % 100);
    }
}

static void param_backend_fm_unpack3(float value, uint8_t *a, uint8_t *b, uint8_t *c)
{
    const uint32_t packed = (uint32_t)param_backend_clamp_value(value, 0.0f, 16777215.0f);
    *a = (uint8_t)packed;
    *b = (uint8_t)(packed >> 8U);
    *c = (uint8_t)(packed >> 16U);
}

static uint8_t param_backend_fm_store_hidden(track_tone_fm_base_voice_t *base,
                                             param_id_t id,
                                             float value)
{
    const uint16_t index = (uint16_t)(id - PARAM_FM_DX7_HIDDEN_FIRST);
    if (index < 24U)
    {
        track_tone_fm_operator_base_t *const op = &base->operators[index / 4U];
        switch (index % 4U)
        {
            case 0U: param_backend_fm_unpack3(value, &op->rates[2], &op->levels[0], &op->levels[1]); break;
            case 1U: param_backend_fm_unpack3(value, &op->levels[3], &op->breakpoint, &op->left_depth); break;
            case 2U: param_backend_fm_unpack3(value, &op->right_depth, &op->left_curve, &op->right_curve); break;
            default: param_backend_fm_unpack3(value, &op->rate_scaling, &op->coarse, &op->fine); break;
        }
        return 1U;
    }
    if (index == 24U) param_backend_fm_unpack3(value, &base->pitch_rates[0], &base->pitch_rates[1], &base->pitch_rates[2]);
    else if (index == 25U) param_backend_fm_unpack3(value, &base->pitch_rates[3], &base->pitch_levels[0], &base->pitch_levels[1]);
    else if (index == 26U) param_backend_fm_unpack3(value, &base->pitch_levels[2], &base->pitch_levels[3], &base->transpose);
    else return 0U;
    return 1U;
}

static uint8_t param_backend_fm_store_ui(track_tone_fm_base_voice_t *base,
                                         param_id_t id,
                                         float value)
{
    if (base == NULL)
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_FM_TRANSPOSE: base->transpose = (uint8_t)(param_backend_clamp_value(value + 24.0f, 0.0f, 48.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_R1: base->pitch_rates[0] = (uint8_t)(param_backend_clamp_value(value, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_R2: base->pitch_rates[1] = (uint8_t)(param_backend_clamp_value(value, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_R3: base->pitch_rates[2] = (uint8_t)(param_backend_clamp_value(value, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_R4: base->pitch_rates[3] = (uint8_t)(param_backend_clamp_value(value, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_L1: base->pitch_levels[0] = (uint8_t)(param_backend_clamp_value(value + 49.0f, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_L2: base->pitch_levels[1] = (uint8_t)(param_backend_clamp_value(value + 49.0f, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_L3: base->pitch_levels[2] = (uint8_t)(param_backend_clamp_value(value + 49.0f, 0.0f, 99.0f) + 0.5f); return 1U;
        case PARAM_FM_PITCH_L4: base->pitch_levels[3] = (uint8_t)(param_backend_clamp_value(value + 49.0f, 0.0f, 99.0f) + 0.5f); return 1U;
        default: return 0U;
    }
}

static float param_backend_fm_macro_unit(float value)
{
    return 0.5f + (0.5f * param_backend_clamp_value(value, -1.0f, 1.0f));
}

static uint8_t param_backend_is_vca_param(param_id_t id)
{
    return (uint8_t)((id == PARAM_VCA_ATTACK)
                     || (id == PARAM_VCA_DECAY)
                     || (id == PARAM_VCA_SUSTAIN)
                     || (id == PARAM_VCA_RELEASE)
                     || (id == PARAM_FILTER_MODE)
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
    const uint16_t logical=(uint16_t)(param_backend_clamp_value(value,0.0f,(float)(MULTI_SAMPLE_POOL_MAX_INSTRUMENTS-1U))+0.5f);
    uint16_t runtime=MULTI_SAMPLE_POOL_INVALID_ID;
    return (project_control_resolve_multi_runtime(logical,&runtime)!=0U)?runtime:MULTI_SAMPLE_POOL_INVALID_ID;
}

static uint8_t param_backend_stream_backend_from_global_selector(float value,
                                                                uint16_t *out_global_slot,
                                                                uint16_t *out_stream_slot)
{
    const uint16_t active_slots = sample_global_pool_get_active_slot_capacity();
    const float max_slot = (active_slots > 0U) ? (float)(active_slots - 1U) : 0.0f;
    const uint16_t logical_slot =
        (uint16_t)(param_backend_clamp_value(value, 0.0f, max_slot) + 0.5f);
    uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    uint16_t stream_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;

    if (out_global_slot != NULL)
    {
        *out_global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }
    if (out_stream_slot != NULL)
    {
        *out_stream_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    }

    uint32_t asset_kind=0U;
    if ((project_control_resolve_sample_runtime(logical_slot,&global_slot,&asset_kind)==0U)
        || (asset_kind!=PERSIST_ASSET_SAMPLE_STREAM)) return 0U;
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
    if (out_global_slot != NULL) *out_global_slot=global_slot;
    return 1U;
}

static uint16_t param_backend_sample_runtime_from_logical(float value)
{
    const uint16_t active_slots = sample_global_pool_get_active_slot_capacity();
    const float max_slot = (active_slots > 0U) ? (float)(active_slots - 1U) : 0.0f;
    const uint16_t logical=(uint16_t)(param_backend_clamp_value(value,0.0f,max_slot)+0.5f);
    uint16_t runtime=SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    return (project_control_resolve_sample_runtime(logical,&runtime,NULL)!=0U)?runtime:SAMPLE_GLOBAL_POOL_INVALID_INDEX;
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
    const track_audio_runtime_ctx_t *const ctx = audio_note_engine_adapter_audio_ctx(track);
    if ((ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->audio_binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_PRISM))
    {
        return 0U;
    }

    const uint8_t instance_id = ctx->audio_binding.instance_id;

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
            const float clamped = param_backend_clamp_value(value, 0.0f, (float)BRICK6_PRISM_LAST_MODEL);
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

uint8_t param_backend_apply_tone_fm(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = (update_base_state != 0U)
        ? track_tone_sound_state_get(track) : NULL;
    const track_audio_runtime_ctx_t *const ctx = audio_note_engine_adapter_audio_ctx(track);
    if ((ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->audio_binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_FM))
    {
        return 0U;
    }

    if ((id >= PARAM_FM_DX7_HIDDEN_FIRST) && (id <= PARAM_FM_DX7_HIDDEN_LAST))
    {
        if (update_base_state != 0U)
        {
            if ((state == NULL) || (param_backend_fm_store_hidden(&state->fm.base, id, value) == 0U))
                return 0U;
            brick6_fm_runtime_set_base_voice(ctx->audio_binding.instance_id, &state->fm.base);
        }
        else
        {
            track_tone_fm_base_voice_t base;
            if ((brick6_fm_runtime_get_base_voice(ctx->audio_binding.instance_id, &base) == 0U)
                    || (param_backend_fm_store_hidden(&base, id, value) == 0U))
                return 0U;
            brick6_fm_runtime_set_base_voice(ctx->audio_binding.instance_id, &base);
        }
        return 1U;
    }

    if ((id >= PARAM_FM_UI_FIRST) && (id <= PARAM_FM_UI_LAST))
    {
        if (update_base_state != 0U)
        {
            if ((state == NULL) || (param_backend_fm_store_ui(&state->fm.base, id, value) == 0U))
                return 0U;
            brick6_fm_runtime_set_base_voice(ctx->audio_binding.instance_id, &state->fm.base);
        }
        else
        {
            track_tone_fm_base_voice_t base;
            if ((brick6_fm_runtime_get_base_voice(ctx->audio_binding.instance_id, &base) == 0U)
                    || (param_backend_fm_store_ui(&base, id, value) == 0U))
                return 0U;
            brick6_fm_runtime_set_base_voice(ctx->audio_binding.instance_id, &base);
        }
        return 1U;
    }

    if (id == PARAM_FM_OPERATOR_SELECT)
    {
        if ((update_base_state != 0U) && (state != NULL))
        {
            state->fm.operator_select = param_backend_clamp_value(value, 0.0f, 5.0f);
        }
        return 1U;
    }

    if ((id >= PARAM_FM_PLAY_VEL) && (id <= PARAM_FM_PLAY_PITCH_TIME))
    {
        track_tone_fm_macros_t local_macros;
        if ((state == NULL)
                && (brick6_fm_runtime_get_macros(ctx->audio_binding.instance_id,
                                                 &local_macros) == 0U))
            return 0U;
        const track_tone_fm_macros_t *const macros = (state != NULL)
            ? &state->fm.macros : &local_macros;
        float velocity = macros->play_vel;
        float key_scaling = macros->play_key;
        float pitch_env = macros->pitch_env;
        float pitch_time = macros->pitch_time;
        if (id == PARAM_FM_PLAY_VEL) velocity = param_backend_clamp_value(value, 0.0f, 1.0f);
        else if (id == PARAM_FM_PLAY_KEY) key_scaling = param_backend_clamp_value(value, 0.0f, 1.0f);
        else if (id == PARAM_FM_PLAY_PITCH_ENV) pitch_env = param_backend_clamp_value(value, -1.0f, 1.0f);
        else pitch_time = param_backend_clamp_value(value, 0.0f, 1.0f);
        if (update_base_state != 0U)
        {
            state->fm.macros.play_vel = velocity;
            state->fm.macros.play_key = key_scaling;
            state->fm.macros.pitch_env = pitch_env;
            state->fm.macros.pitch_time = pitch_time;
        }
        brick6_fm_runtime_set_play(ctx->audio_binding.instance_id, velocity, key_scaling, pitch_env, pitch_time);
        return 1U;
    }

    if ((id >= PARAM_FM_OPERATOR_FIRST) && (id <= PARAM_FM_OPERATOR_LAST))
    {
        const uint16_t offset = (uint16_t)(id - PARAM_FM_OPERATOR_FIRST);
        const uint8_t operator_id = (uint8_t)(offset / PARAM_FM_OPERATOR_PARAM_COUNT);
        const brick6_fm_operator_param_t operator_param =
            (brick6_fm_operator_param_t)(offset % PARAM_FM_OPERATOR_PARAM_COUNT);
        if (update_base_state != 0U)
        {
            if (state == NULL)
                return 0U;
            track_tone_fm_operator_base_t *const op = &state->fm.base.operators[operator_id];
            switch (operator_param)
            {
                case BRICK6_FM_OPERATOR_LEVEL: op->output_level = (uint8_t)(value + 0.5f); break;
                case BRICK6_FM_OPERATOR_FREQ: param_backend_fm_store_frequency(op, value); break;
                case BRICK6_FM_OPERATOR_DETUNE: op->detune = (int8_t)value; break;
                case BRICK6_FM_OPERATOR_ENV_ATTACK: op->rates[0] = (uint8_t)(value + 0.5f); break;
                case BRICK6_FM_OPERATOR_ENV_DECAY: op->rates[1] = (uint8_t)(value + 0.5f); break;
                case BRICK6_FM_OPERATOR_ENV_SUSTAIN: op->levels[2] = (uint8_t)(value + 0.5f); break;
                case BRICK6_FM_OPERATOR_ENV_RELEASE: op->rates[3] = (uint8_t)(value + 0.5f); break;
                case BRICK6_FM_OPERATOR_ON: op->enabled = (value >= 0.5f) ? 1U : 0U; break;
                case BRICK6_FM_OPERATOR_MODE: op->mode = (value >= 0.5f) ? 1U : 0U; break;
                case BRICK6_FM_OPERATOR_VEL: op->velocity_sensitivity =
                    (uint8_t)(param_backend_clamp_value(value, 0.0f, 1.0f) * 7.0f + 0.5f); break;
                case BRICK6_FM_OPERATOR_KEY:
                {
                    const uint8_t depth = (uint8_t)(param_backend_clamp_value(value, 0.0f, 1.0f) * 99.0f + 0.5f);
                    op->left_depth = depth;
                    op->right_depth = depth;
                    break;
                }
                default: break;
            }
        }
        if ((update_base_state != 0U) && (state != NULL))
            brick6_fm_runtime_set_base_voice(ctx->audio_binding.instance_id, &state->fm.base);
        else
            brick6_fm_runtime_set_operator(ctx->audio_binding.instance_id, operator_id, operator_param, value);
        return 1U;
    }

    switch (id)
    {
        case PARAM_FM_RATIO:
        {
            const float ratio = param_backend_clamp_value(value, -1.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL)) state->fm.macros.ratio = ratio;
            brick6_fm_runtime_set_ratio(ctx->audio_binding.instance_id, param_backend_fm_macro_unit(ratio));
            return 1U;
        }
        case PARAM_FM_ALGORITHM:
        {
            const uint8_t algorithm = (uint8_t)(param_backend_clamp_value(value, 0.0f, 31.0f) + 0.5f);
            if ((update_base_state != 0U) && (state != NULL)) state->fm.base.algorithm = algorithm;
            brick6_fm_runtime_set_algorithm(ctx->audio_binding.instance_id, algorithm);
            return 1U;
        }
        case PARAM_FM_FEEDBACK:
        {
            const uint8_t feedback = (uint8_t)(param_backend_clamp_value(value, 0.0f, 7.0f) + 0.5f);
            if ((update_base_state != 0U) && (state != NULL)) state->fm.base.feedback = feedback;
            brick6_fm_runtime_set_feedback(ctx->audio_binding.instance_id, feedback);
            return 1U;
        }
        case PARAM_FM_SYNC:
        {
            const uint8_t sync = (value >= 0.5f) ? 1U : 0U;
            if ((update_base_state != 0U) && (state != NULL)) state->fm.base.key_sync = sync;
            brick6_fm_runtime_set_sync(ctx->audio_binding.instance_id, sync);
            return 1U;
        }
        case PARAM_FM_BRIGHT:
        case PARAM_FM_BODY:
        case PARAM_FM_DETAIL:
        case PARAM_FM_METAL:
        {
            const float macro = param_backend_clamp_value(value, -1.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                if (id == PARAM_FM_BRIGHT) state->fm.macros.bright = macro;
                else if (id == PARAM_FM_BODY) state->fm.macros.body = macro;
                else if (id == PARAM_FM_DETAIL) state->fm.macros.detail = macro;
                else state->fm.macros.metal = macro;
            }
            if (id == PARAM_FM_BRIGHT) brick6_fm_runtime_set_bright(ctx->audio_binding.instance_id, param_backend_fm_macro_unit(macro));
            else if (id == PARAM_FM_BODY) brick6_fm_runtime_set_body(ctx->audio_binding.instance_id, param_backend_fm_macro_unit(macro));
            else if (id == PARAM_FM_DETAIL) brick6_fm_runtime_set_detail(ctx->audio_binding.instance_id, param_backend_fm_macro_unit(macro));
            else brick6_fm_runtime_set_metal(ctx->audio_binding.instance_id, param_backend_fm_macro_unit(macro));
            return 1U;
        }
        case PARAM_FM_ENV_ATTACK:
        case PARAM_FM_ENV_DECAY:
        case PARAM_FM_ENV_SUSTAIN:
        case PARAM_FM_ENV_RELEASE:
        {
            const float macro = param_backend_clamp_value(value, -1.0f, 1.0f);
            track_tone_fm_macros_t local_macros;
            if ((state == NULL)
                    && (brick6_fm_runtime_get_macros(ctx->audio_binding.instance_id,
                                                     &local_macros) == 0U))
                return 0U;
            const track_tone_fm_macros_t *const macros = (state != NULL)
                ? &state->fm.macros : &local_macros;
            float attack = macros->env_attack;
            float decay = macros->env_decay;
            float sustain = macros->env_sustain;
            float release = macros->env_release;
            if (id == PARAM_FM_ENV_ATTACK) attack = macro;
            else if (id == PARAM_FM_ENV_DECAY) decay = macro;
            else if (id == PARAM_FM_ENV_SUSTAIN) sustain = macro;
            else release = macro;
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->fm.macros.env_attack = attack;
                state->fm.macros.env_decay = decay;
                state->fm.macros.env_sustain = sustain;
                state->fm.macros.env_release = release;
            }
            brick6_fm_runtime_set_env(ctx->audio_binding.instance_id,
                                      param_backend_fm_macro_unit(attack),
                                      param_backend_fm_macro_unit(decay),
                                      param_backend_fm_macro_unit(sustain),
                                      param_backend_fm_macro_unit(release));
            return 1U;
        }
        default:
            return 0U;
    }
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
    if ((id >= PARAM_STACK_OSC1_MODEL) && (id <= PARAM_STACK_OSC3_COLOR))
    {
        const uint8_t rel = (uint8_t)(id - PARAM_STACK_OSC1_MODEL);
        *out_slot = (uint8_t)(rel / 4U);
        *out_param = (uint8_t)((rel % 4U) + 1U);
        return (*out_slot < BRICK6_STACK_SLOT_COUNT) ? 1U : 0U;
    }

    return 0U;
}

uint8_t param_backend_apply_tone_stack(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_audio_runtime_ctx_t *const ctx = audio_note_engine_adapter_audio_ctx(track);
    if ((ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->audio_binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            || (ctx->audio_binding.instance_id >= BRICK6_STACK_VOICE_INSTANCE_COUNT))
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
            brick6_stack_runtime_set_noise_level(ctx->audio_binding.instance_id, clamped);
            return 1U;
        }
        return brick6_stack_runtime_submit_noise_level(ctx->audio_binding.instance_id, clamped);
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
            brick6_stack_runtime_set_osc_detune(ctx->audio_binding.instance_id, clamped);
            return 1U;
        }
        return brick6_stack_runtime_submit_osc_detune(ctx->audio_binding.instance_id, clamped);
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
            brick6_stack_runtime_set_phase_reset(ctx->audio_binding.instance_id, enabled);
            return 1U;
        }
        return brick6_stack_runtime_submit_phase_reset(ctx->audio_binding.instance_id, enabled);
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
                brick6_stack_runtime_set_slot_level(ctx->audio_binding.instance_id, slot, clamped);
                return 1U;
            }
            return brick6_stack_runtime_submit_slot_level(ctx->audio_binding.instance_id, slot, clamped);
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
                brick6_stack_runtime_set_slot_model(ctx->audio_binding.instance_id, slot, model);
                return 1U;
            }
            return brick6_stack_runtime_submit_slot_model(ctx->audio_binding.instance_id, slot, model);
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
                brick6_stack_runtime_set_slot_tune(ctx->audio_binding.instance_id, slot, clamped);
                return 1U;
            }
            return brick6_stack_runtime_submit_slot_tune(ctx->audio_binding.instance_id, slot, clamped);
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
                brick6_stack_runtime_set_slot_timbre(ctx->audio_binding.instance_id, slot, clamped);
                return 1U;
            }
            return brick6_stack_runtime_submit_slot_timbre(ctx->audio_binding.instance_id, slot, clamped);
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
                brick6_stack_runtime_set_slot_color(ctx->audio_binding.instance_id, slot, clamped);
                return 1U;
            }
            return brick6_stack_runtime_submit_slot_color(ctx->audio_binding.instance_id, slot, clamped);
        }
        default:
            return 0U;
    }
}

static uint8_t param_backend_wave_slot_for_id(param_id_t id, uint8_t *out_osc, uint8_t *out_param)
{
    if ((out_osc == NULL) || (out_param == NULL)
            || (id < PARAM_WAVE_OSC1_TABLE)
            || (id > PARAM_WAVE_OSC2_TUNE))
    {
        return 0U;
    }

    const uint8_t rel = (uint8_t)(id - PARAM_WAVE_OSC1_TABLE);
    *out_osc = (uint8_t)(rel / 6U);
    *out_param = (uint8_t)(rel % 6U);
    return (*out_osc < BRICK6_WAVE_OSC_COUNT) ? 1U : 0U;
}

uint8_t param_backend_apply_tone_wave(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = (update_base_state != 0U)
        ? track_tone_sound_state_get(track) : NULL;
    const track_audio_runtime_ctx_t *const ctx = audio_note_engine_adapter_audio_ctx(track);
    if ((ctx == NULL)
            || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND)
            || (ctx->audio_binding.engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            || (ctx->audio_binding.instance_id >= BRICK6_WAVE_VOICE_INSTANCE_COUNT))
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
        brick6_wave_runtime_set_frame_interp(ctx->audio_binding.instance_id, enabled);
        return 1U;
    }
    if (id == PARAM_WAVE_SAMPLE_INTERP)
    {
        const uint8_t enabled = (value >= 0.5f) ? 1U : 0U;
        if ((update_base_state != 0U) && (state != NULL))
        {
            state->wave.sample_interp = (float)enabled;
        }
        brick6_wave_runtime_set_sample_interp(ctx->audio_binding.instance_id, enabled);
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
        brick6_wave_runtime_set_pos_update(ctx->audio_binding.instance_id, update);
        return 1U;
    }
    if (id == PARAM_WAVE_POS_SMOOTH)
    {
        const uint8_t enabled = (value >= 0.5f) ? 1U : 0U;
        if ((update_base_state != 0U) && (state != NULL))
        {
            state->wave.pos_smooth = (float)enabled;
        }
        brick6_wave_runtime_set_pos_smooth(ctx->audio_binding.instance_id, enabled);
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
            const uint16_t logical_slot = (uint16_t)(param_backend_clamp_value(value, 0.0f, (float)(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS - 1U)) + 0.5f);
            if (update_base_state != 0U)
            {
                if (audio_wave_table_projection_publish_track(
                        track, osc, logical_slot) == 0U)
                    return 0U;
            }
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.table[osc] = (float)logical_slot;
            }
            return 1U;
        }
        case 1U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.pos[osc] = clamped;
            }
            brick6_wave_runtime_set_osc_pos(ctx->audio_binding.instance_id, osc, clamped);
            return 1U;
        }
        case 2U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.start[osc] = clamped;
            }
            brick6_wave_runtime_set_osc_start(ctx->audio_binding.instance_id, osc, clamped);
            return 1U;
        }
        case 3U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.end[osc] = clamped;
            }
            brick6_wave_runtime_set_osc_end(ctx->audio_binding.instance_id, osc, clamped);
            return 1U;
        }
        case 4U:
        {
            const float clamped = param_backend_clamp_value(value, 0.0f, 1.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.level[osc] = clamped;
            }
            brick6_wave_runtime_set_osc_level(ctx->audio_binding.instance_id, osc, clamped);
            return 1U;
        }
        case 5U:
        {
            const float clamped = param_backend_clamp_value(value, -60.0f, 60.0f);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->wave.tune[osc] = clamped;
            }
            brick6_wave_runtime_set_osc_tune(ctx->audio_binding.instance_id, osc, clamped);
            return 1U;
        }
        default:
            return 0U;
    }
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
    if (ctx == NULL)
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

uint8_t param_backend_send_midi_cc_audio(
    const track_audio_runtime_ctx_t *ctx,
    param_id_t id,
    float value)
{
    if ((ctx == NULL) || (param_backend_is_midi_cc_id(id) == 0U))
    {
        return 0U;
    }

    uint8_t channel = 0U;
    if (audio_note_engine_adapter_audio_midi_channel_zero_based(
            ctx, &channel) == 0U)
    {
        return 0U;
    }

    midi_cc(MIDI_DEST_BOTH,
            channel,
            param_backend_midi_cc_number_from_id(id),
            (uint8_t)(param_backend_clamp_value(value, 0.0f, 127.0f) + 0.5f));
    return 1U;
}

uint8_t param_backend_apply_tone_sampler(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);
    const track_audio_runtime_ctx_t *const ctx = audio_note_engine_adapter_audio_ctx(track);

    switch (id)
    {
        case PARAM_SAMPLER_SAMPLE:
            if ((ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))
            {
                const uint16_t logical=(uint16_t)(param_backend_clamp_value(value,0.0f,(float)(MULTI_SAMPLE_POOL_MAX_INSTRUMENTS-1U))+0.5f);
                if(project_control_has_multi(logical)==0U)return 0U;
                if ((update_base_state != 0U) && (state != NULL))
                {
                    state->sample = (float)logical;
                }
                const uint16_t runtime=param_backend_multi_instrument_from_selector(value);
                if(runtime==MULTI_SAMPLE_POOL_INVALID_ID){brick6_sampler_runtime_set_multi_instrument(track,runtime);return 1U;}
                brick6_sampler_runtime_set_multi_instrument(track,runtime);
                return 1U;
            }
        {
            uint16_t global_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
            uint16_t stream_slot = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
            if ((ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
            {
                const uint16_t logical=(uint16_t)(param_backend_clamp_value(value,0.0f,(float)(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS-1U))+0.5f);
                uint32_t kind=0U;
                if(project_control_has_sample(logical,&kind)==0U||kind!=PERSIST_ASSET_SAMPLE_STREAM)return 0U;
                if (param_backend_stream_backend_from_global_selector(value,
                                                                      &global_slot,
                                                                      &stream_slot) == 0U)
                {
                    if ((update_base_state != 0U) && (state != NULL))state->sample=(float)logical;
                    brick6_sampler_runtime_stop(track);
                    return 1U;
                }
                if ((update_base_state != 0U) && (state != NULL))
                {
                    state->sample = value;
                }
                brick6_sampler_runtime_set_sample(track, stream_slot);
                return 1U;
            }

            const uint16_t logical_slot=(uint16_t)(param_backend_clamp_value(value,0.0f,(float)(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS-1U))+0.5f);
            uint32_t sample_kind=0U;
            if(project_control_has_sample(logical_slot,&sample_kind)==0U)return 0U;
            if((ctx!=NULL)&&(ctx->type==(uint8_t)TRACK_RUNTIME_TYPE_RAM)&&(sample_kind!=PERSIST_ASSET_SAMPLE_RAM))return 0U;
            global_slot = param_backend_sample_runtime_from_logical(value);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->sample = (float)logical_slot;
            }
            if(global_slot==SAMPLE_GLOBAL_POOL_INVALID_INDEX){brick6_sampler_runtime_stop(track);return 1U;}
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
    const track_audio_runtime_ctx_t *const ctx = audio_note_engine_adapter_audio_ctx(track);

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
                                      const track_audio_runtime_ctx_t *ctx,
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

    return drum_synth_set_param_for_instance(ctx->audio_binding.instance_id, id, value);
}

uint8_t param_backend_apply_mix_track(const track_audio_runtime_ctx_t *ctx,
                                      uint8_t track,
                                      param_id_t id,
                                      float value,
                                      uint8_t update_base_state)
{
    if ((ctx == NULL) || (audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U))
    {
        return 0U;
    }

    if ((param_backend_is_vca_param(id) != 0U)
            && (audio_note_engine_adapter_ctx_supports_vca_gate(ctx) == 0U))
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
            mixer_set_track_gain(ctx->audio_binding.mix_track_id, param_backend_clamp_value(value, 0.0f, 2.0f));
            return 1U;
        }

        case PARAM_MIX_PAN:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_pan = param_backend_clamp_value(value, -1.0f, 1.0f);
            }
            mixer_set_track_pan(ctx->audio_binding.mix_track_id, param_backend_clamp_value(value, -1.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND1:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_send1 = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            mixer_set_track_send_level(ctx->audio_binding.mix_track_id, 0U, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND2:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->mix_send2 = param_backend_clamp_value(value, 0.0f, 1.0f);
            }
            mixer_set_track_send_level(ctx->audio_binding.mix_track_id, 1U, param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_SEND3:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
                state->mix_send3 = param_backend_clamp_value(value, 0.0f, 1.0f);
            mixer_set_track_send_level(ctx->audio_binding.mix_track_id, 2U,
                                       param_backend_clamp_value(value, 0.0f, 1.0f));
            return 1U;
        }

        case PARAM_MIX_MUTE:
            return audio_note_engine_adapter_set_mute(
                track, (value >= 0.5f) ? 1U : 0U);

        case PARAM_ENV_RETRIG_FILTER:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            const uint8_t hard = (value >= 0.5f) ? 1U : 0U;
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->env_retrig_filter = (float)hard;
            }

            if (((ctx->flags & 1U) != 0U)
                    && (ctx->audio_binding.mix_track_id < MIXER_MAX_TRACKS))
            {
                mixer_set_track_filter_retrigger_hard(
                    ctx->audio_binding.mix_track_id, hard);
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
            mixer_set_track_vca_retrigger_hard(ctx->audio_binding.mix_track_id, hard);
            return 1U;
        }

        case PARAM_FILTER_MODE:
        {
            const uint8_t mode = (uint8_t)(param_backend_clamp_value(value, 0.0f, 2.0f) + 0.5f);
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->filter_mode = (float)mode;
            }
            mixer_set_track_filter_mode(ctx->audio_binding.mix_track_id, mode);
            return 1U;
        }

        case PARAM_VCA_ATTACK:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->vca_attack = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_attack(ctx->audio_binding.mix_track_id, param_filter_ui127_to_attack_s(value));
            return 1U;
        }

        case PARAM_VCA_DECAY:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->vca_decay = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_decay(ctx->audio_binding.mix_track_id, param_filter_ui127_to_decay_s(value));
            return 1U;
        }

        case PARAM_VCA_SUSTAIN:
        {
            track_sound_state_t *state = track_sound_state_get(track);
            if ((update_base_state != 0U) && (state != NULL))
            {
                state->vca_sustain = param_backend_clamp_value(value, 0.0f, 127.0f);
            }
            mixer_set_track_vca_sustain(ctx->audio_binding.mix_track_id, param_filter_ui127_to_sustain(value));
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
            mixer_set_track_vca_release(ctx->audio_binding.mix_track_id, release_s);
            if (ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            {
                brick6_braids_runtime_set_vca_release_seconds(ctx->audio_binding.instance_id, release_s);
            }
            return 1U;
        }

        default:
            return 0U;
    }
}

uint8_t param_backend_apply_env_track(const track_audio_runtime_ctx_t *ctx, param_id_t id, float value)
{
    if ((ctx == NULL) || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    switch (ctx->audio_binding.engine)
    {
        case (uint8_t)TRACK_RUNTIME_ENGINE_DRUM:
            return drum_synth_set_param_for_instance(ctx->audio_binding.instance_id, id, value);
        default:
            return 0U;
    }
}
