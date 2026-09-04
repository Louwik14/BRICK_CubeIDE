#include "IPC/control_audio_visual.h"

#include "App/control_domain.h"

uint8_t control_audio_visual_waveform_request(brick_entity_id_t entity,
                                               uint8_t enabled,
                                               uint8_t fast_refresh)
{
    if (enabled == 0U) entity = 0U;
    const control_audio_visual_intent_t intent = {
        .operation = 0U,
        .entity = entity,
        .slot = 0U,
        .value = (enabled ? 1U : 0U) | ((fast_refresh ? 1U : 0U) << 1)
    };
    return control_domain_request_audio_visual(&intent);
}

uint8_t control_audio_visual_synth_request(uint8_t enabled,
                                           brick_entity_id_t entity,
                                           synth_waveform_engine_t engine,
                                           uint8_t osc_mask)
{
    if (enabled == 0U)
    {
        entity = 0U;
        engine = SYNTH_WAVEFORM_ENGINE_NONE;
        osc_mask = 0U;
    }
    const control_audio_visual_intent_t intent = {
        .operation = 1U,
        .entity = entity,
        .slot = (uint8_t)engine,
        .value = (uint8_t)(osc_mask & 3U)
    };
    return control_domain_request_audio_visual(&intent);
}
