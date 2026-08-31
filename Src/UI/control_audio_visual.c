#include "IPC/control_audio_visual.h"

#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "IPC/live_clock_control.h"

static uint8_t visual_publish(uint8_t entity, uint16_t id, uint32_t value)
{
    uint64_t sample=0U;
    if (!live_clock_read_audio_sample(&sample)) return 0U;
    return control_audio_publish_param(entity,id,value,0U,sample);
}

uint8_t control_audio_visual_waveform_request(brick_entity_id_t entity,
                                               uint8_t enabled,
                                               uint8_t fast_refresh)
{
    if (enabled == 0U) entity=0U;
    return visual_publish(entity,CONTROL_AUDIO_PARAM_AUDIO_WAVEFORM_REQUEST,
        (enabled ? 1U:0U)|((fast_refresh?1U:0U)<<1));
}

uint8_t control_audio_visual_synth_request(uint8_t enabled,
                                           brick_entity_id_t entity,
                                           synth_waveform_engine_t engine,
                                           uint8_t osc_mask)
{
    if (enabled == 0U) { entity=0U; engine=SYNTH_WAVEFORM_ENGINE_NONE; osc_mask=0U; }
    return visual_publish(entity,CONTROL_AUDIO_PARAM_SYNTH_WAVEFORM_REQUEST,
        (uint32_t)engine|((uint32_t)(osc_mask&3U)<<8));
}
