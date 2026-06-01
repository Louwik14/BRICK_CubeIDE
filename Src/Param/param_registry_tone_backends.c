#include "Param/param_registry_backends.h"
#include "Param/param_registry_runtime_state.h"

#include "Audio/fx_master_macro.h"
#include "Core/track_tone_sound_state.h"
#include <stddef.h>

static uint8_t param_backend_master_fx_type_slot(param_id_t id, uint8_t *out_slot)
{
    if ((id != PARAM_MASTER_FX1_TYPE)
            && (id != PARAM_MASTER_FX2_TYPE)
            && (id != PARAM_MASTER_FX3_TYPE)
            && (id != PARAM_MASTER_FX4_TYPE))
    {
        return 0U;
    }
    if (out_slot != NULL)
    {
        *out_slot = (uint8_t)((id - PARAM_MASTER_FX1_TYPE) / 4U);
    }
    return 1U;
}

static float param_backend_normalize_master_fx_type(uint8_t track, param_id_t id, float value)
{
    uint8_t slot = 0U;
    if (param_backend_master_fx_type_slot(id, &slot) == 0U)
    {
        return value;
    }

    uint8_t type = 0U;
    if (value > 0.0f)
    {
        type = (uint8_t)(value + 0.5f);
    }
    if (type >= (uint8_t)FX_MASTER_MACRO_TYPE_COUNT)
    {
        type = (uint8_t)FX_MASTER_MACRO_COLOR;
    }

    if (type != (uint8_t)FX_MASTER_MACRO_STUTTER)
    {
        return (float)type;
    }

    const track_tone_sound_state_t *const state = track_tone_sound_state_get_const(track);
    if (state == NULL)
    {
        return (float)type;
    }

    for (uint8_t other = 0U; other < slot; ++other)
    {
        if ((uint8_t)(state->master_fx.type[other] + 0.5f) == (uint8_t)FX_MASTER_MACRO_STUTTER)
        {
            return (float)FX_MASTER_MACRO_OFF;
        }
    }

    return (float)type;
}

uint8_t param_backend_apply_track_value(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX))
    {
        return 0U;
    }

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (param_backend_track_supports_midi_tone_ctx(ctx) != 0U))
    {
        if (param_backend_is_midi_cc_id(id) != 0U)
        {
            if (param_backend_send_midi_cc(track, id, value) == 0U)
            {
                return 0U;
            }
            if (update_base_state != 0U)
            {
                param_registry_runtime_commit_authoritative_write(track, id, value, 0U);
            }
            return 1U;
        }

        if (id == PARAM_MIDI_PROGRAM)
        {
            return 0U;
        }
    }

    float effective_value = value;
    if ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MASTER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MASTER_FX))
    {
        effective_value = param_backend_normalize_master_fx_type(track, id, value);
    }

    uint8_t applied = 0U;
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
    {
        applied = param_backend_apply_mix_track(ctx, track, id, effective_value, update_base_state);
    }
    else if ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MASTER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MASTER_FX))
    {
        applied = param_backend_apply_master_fx_track(ctx, track, id, effective_value, update_base_state);
    }
    else if ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
    {
        applied = param_backend_apply_tone_looper(track, id, effective_value, update_base_state);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        applied = param_backend_apply_tone_sampler(track, id, effective_value, update_base_state);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
    {
        applied = param_backend_apply_tone_wave(track, id, effective_value, update_base_state);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
    {
        applied = param_backend_apply_tone_drum(track, ctx, id, effective_value, update_base_state);
    }

    if ((applied != 0U) && (update_base_state != 0U))
    {
        param_registry_runtime_commit_authoritative_write(track, id, effective_value, 0U);
    }

    return applied;
}
