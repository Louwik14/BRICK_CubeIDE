#include "Param/param_registry_backends.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/audio_mod_matrix.h"
#include "Core/live_parameter_audio_runtime.h"
#include "Param/param_registry.h"

#include <stddef.h>

uint8_t param_backend_apply_track_value_control(
    uint8_t track, param_id_t id, float value)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            || (param_backend_track_supports_midi_tone_ctx(ctx) == 0U))
    {
        return 0U;
    }

    if (param_backend_is_midi_cc_id(id) != 0U)
        return param_backend_send_midi_cc(track, id, value);

    return 0U;
}

uint8_t param_backend_apply_prepared_track_value_audio(
    uint8_t track,
    param_id_t id,
    float value)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    const uint8_t uses_mix_backend = (uint8_t)(((rule.resource == TRACK_RUNTIME_RESOURCE_MIX)
                                                && ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
                                                    || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)))
                                               || ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
                                                   && (id == PARAM_ENV_RETRIG_FILTER)));
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (uses_mix_backend == 0U))
    {
        return 0U;
    }

    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    if ((ctx == NULL) || (ctx->program_route.active == 0U))
    {
        return 0U;
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI)
                || ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL)
                    && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_EXTERNAL))))
    {
        if (param_backend_is_midi_cc_id(id) != 0U)
        {
            if (param_backend_send_midi_cc_audio(ctx, id, value) == 0U)
            {
                return 0U;
            }
            return 1U;
        }

        if (id == PARAM_MIDI_PROGRAM)
        {
            return 0U;
        }
    }

    const float effective_value = value;

    uint8_t applied = 0U;
    if (uses_mix_backend != 0U)
    {
        applied = param_backend_apply_mix_track(ctx, track, id, effective_value);
    }
    else if ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
    {
        applied = param_backend_apply_tone_looper(track, id, effective_value);
    }
    else if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        applied = param_backend_apply_tone_sampler(track, id, effective_value);
    }
    else if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
    {
        applied = param_backend_apply_tone_prism(track, id, effective_value);
    }
    else if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_FM)
    {
        applied = param_backend_apply_tone_fm(track, id, effective_value);
    }
    else if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
    {
        applied = param_backend_apply_tone_stack(track, id, effective_value);
    }
    else if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
    {
        applied = param_backend_apply_tone_wave(track, id, effective_value);
    }
    else if (ctx->program_route.engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
    {
        applied = param_backend_apply_tone_drum(track, ctx, id, effective_value);
    }

    return applied;
}

uint8_t param_backend_prepare_current_tone_projection(uint8_t track,
                                                       uint8_t runtime_type)
{
    if ((runtime_type == (uint8_t)TRACK_RUNTIME_TYPE_MIDI)
            || (runtime_type == (uint8_t)TRACK_RUNTIME_TYPE_EXTERNAL))
        return 1U;
    for (uint8_t slot = 0U; slot < SEQ_PARAM_TONE_SLOT_COUNT; ++slot)
    {
        param_id_t id;
        float normalized;
        if (track_runtime_tone_slot_to_param((track_runtime_type_t)runtime_type,
                                             slot, &id) == 0U)
            continue;
        if (param_registry_track_value_is_audio_command(id, track) == 0U) continue;
        if (live_parameter_audio_runtime_tone_get(track, slot, &normalized) == 0U)
            return 0U;
        const float value = param_registry[id].min
            + normalized * (param_registry[id].max - param_registry[id].min);
        if (param_backend_apply_prepared_track_value_audio(track, id, value) == 0U)
            return 0U;
        audio_mod_matrix_base_update(track, id, value);
    }
    return 1U;
}
