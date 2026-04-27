#include "Param/param_registry_backends.h"
#include "Param/param_registry_runtime_state.h"

#include <stddef.h>

uint8_t param_backend_apply_track_value(uint8_t track, param_id_t id, float value, uint8_t update_base_state)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_BUFFER)
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
            param_registry_runtime_commit_authoritative_write(track, id, value, 0U);
            return 1U;
        }

        if (id == PARAM_MIDI_PROGRAM)
        {
            return 0U;
        }
    }

    uint8_t applied = 0U;
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
    {
        applied = param_backend_apply_mix_track(ctx, track, id, value, update_base_state);
    }
    else if (rule.resource == TRACK_RUNTIME_RESOURCE_BUFFER)
    {
        applied = param_backend_apply_buffer_track(ctx, track, id, value);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        applied = param_backend_apply_tone_sampler(track, id, value, update_base_state);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_PLAITS)
    {
        applied = param_backend_apply_tone_plaits(track, id, value, update_base_state);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
    {
        applied = param_backend_apply_tone_drum(track, ctx, id, value, update_base_state);
    }

    if (applied != 0U)
    {
        param_registry_runtime_commit_authoritative_write(track, id, value, 0U);
    }

    return applied;
}
