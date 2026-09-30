#include "ControlRT/control_audio_visual.h"

#include "ControlRT/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"

static uint8_t control_audio_visual_publish(uint8_t entity, uint16_t id, uint32_t value)
{
    return control_rt_publish_param_now(entity, id, value, 0U);
}

static brick_entity_id_t g_waveform_pending_entity;
static uint32_t g_waveform_pending_value;
static uint8_t g_waveform_pending;

uint8_t control_audio_visual_waveform_request(brick_entity_id_t entity,
                                               uint8_t enabled,
                                               uint8_t fast_refresh)
{
    if (enabled == 0U) entity = 0U;
    g_waveform_pending_entity = entity;
    g_waveform_pending_value = (enabled ? 1U : 0U)
        | ((fast_refresh ? 1U : 0U) << 1);
    g_waveform_pending = (uint8_t)(control_audio_visual_publish(entity,
        CONTROL_AUDIO_PARAM_AUDIO_WAVEFORM_REQUEST,
        g_waveform_pending_value) == 0U);
    return (uint8_t)(g_waveform_pending == 0U);
}

void control_audio_visual_service(void)
{
    if (g_waveform_pending == 0U) return;
    g_waveform_pending = (uint8_t)(control_audio_visual_publish(
        g_waveform_pending_entity, CONTROL_AUDIO_PARAM_AUDIO_WAVEFORM_REQUEST,
        g_waveform_pending_value) == 0U);
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
    return control_audio_visual_publish(entity,
        CONTROL_AUDIO_PARAM_SYNTH_WAVEFORM_REQUEST,
        (uint32_t)engine | ((uint32_t)(osc_mask & 3U) << 8));
}
