#include "Audio/audio_note_engine_adapter.h"

#include <stddef.h>

track_runtime_engine_t audio_note_engine_adapter_choose_engine(
    track_runtime_family_t family, track_runtime_type_t type)
{
    if ((family == TRACK_RUNTIME_FAMILY_EXTERNAL)
            && (type == TRACK_RUNTIME_TYPE_EXTERNAL))
        return TRACK_RUNTIME_ENGINE_AUDIO_TRACK;
    if (family == TRACK_RUNTIME_FAMILY_DRUM)
        return ((type == TRACK_RUNTIME_TYPE_DRUM_MD)
                || (type == TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG))
            ? TRACK_RUNTIME_ENGINE_DRUM : TRACK_RUNTIME_ENGINE_NONE;
    if (family == TRACK_RUNTIME_FAMILY_SAMPLER)
    {
        if (type == TRACK_RUNTIME_TYPE_LOOPER) return TRACK_RUNTIME_ENGINE_LOOPER;
        if ((type == TRACK_RUNTIME_TYPE_RAM) || (type == TRACK_RUNTIME_TYPE_STREAM)
                || (type == TRACK_RUNTIME_TYPE_MULTI))
            return TRACK_RUNTIME_ENGINE_SAMPLER;
    }
    if (family == TRACK_RUNTIME_FAMILY_SYNTH)
    {
        if (type == TRACK_RUNTIME_TYPE_PRISM) return TRACK_RUNTIME_ENGINE_PRISM;
        if (type == TRACK_RUNTIME_TYPE_STACK) return TRACK_RUNTIME_ENGINE_STACK;
        if (type == TRACK_RUNTIME_TYPE_WAVE) return TRACK_RUNTIME_ENGINE_WAVE;
        if (type == TRACK_RUNTIME_TYPE_FM) return TRACK_RUNTIME_ENGINE_FM;
    }
    return TRACK_RUNTIME_ENGINE_NONE;
}

uint8_t audio_note_engine_adapter_prepare_install_spec(
    const control_audio_event_t *event,
    audio_note_engine_install_spec_t *out_spec)
{
    if ((event == NULL)
            || (out_spec == NULL)
            || (event->entity_id >= BRICK_ENTITY_CAPACITY))
    {
        return 0U;
    }

    union { uint32_t u; float f; } spread = {
        .u = event->source_generation
    };
    *out_spec = (audio_note_engine_install_spec_t){
        .entity_id = event->entity_id,
        .family = event->provenance,
        .type = event->note,
        .midi_channel_1_16 = event->velocity,
        .midi_source = (uint8_t)event->param_value,
        .flags = event->flags,
        .voice_count = (event->param_id != 0U)
            ? (uint8_t)event->param_id : 1U,
        .voice_spread = spread.f
    };
    return 1U;
}
