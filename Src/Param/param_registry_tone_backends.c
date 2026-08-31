#include "Param/param_registry_backends.h"
#include "Audio/audio_note_engine_adapter.h"
#include <stddef.h>

uint8_t param_backend_apply_prepared_track_value_audio(
    uint8_t track,
    param_id_t id,
    float value)
{
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    if ((ctx == NULL) || (ctx->program_route.active == 0U))
    {
        return 0U;
    }

    const float effective_value = value;

    uint8_t applied = param_backend_apply_mix_track(
        ctx, track, id, effective_value);
    if (applied != 0U) return applied;
    if ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
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
