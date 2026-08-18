#include "Param/param_registry_backends.h"
#include "Audio/audio_note_engine_adapter.h"

#include "Core/track_tone_sound_state.h"
#include <stddef.h>

uint8_t param_backend_apply_track_value(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
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

    const track_audio_runtime_ctx_t *const ctx = audio_note_engine_adapter_audio_ctx(track);
    if ((ctx == NULL) || (ctx->audio_binding.bind_state != TRACK_RUNTIME_BIND_BOUND))
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
            if (param_backend_send_midi_cc(track, id, value) == 0U)
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
        applied = param_backend_apply_mix_track(ctx, track, id, effective_value, update_base_state);
    }
    else if ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
    {
        applied = param_backend_apply_tone_looper(track, id, effective_value, update_base_state);
    }
    else if (ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        applied = param_backend_apply_tone_sampler(track, id, effective_value, update_base_state);
    }
    else if (ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
    {
        applied = param_backend_apply_tone_prism(track, id, effective_value, update_base_state);
    }
    else if (ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_FM)
    {
        applied = param_backend_apply_tone_fm(track, id, effective_value, update_base_state);
    }
    else if (ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
    {
        applied = param_backend_apply_tone_stack(track, id, effective_value, update_base_state);
    }
    else if (ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
    {
        applied = param_backend_apply_tone_wave(track, id, effective_value, update_base_state);
    }
    else if (ctx->audio_binding.engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
    {
        applied = param_backend_apply_tone_drum(track, ctx, id, effective_value, update_base_state);
    }

    return applied;
}
