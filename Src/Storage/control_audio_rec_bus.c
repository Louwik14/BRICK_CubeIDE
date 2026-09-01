#include "IPC/control_audio_rec_bus.h"

#include "IPC/control_audio_command.h"
#include "ControlRT/control_rt_publication.h"
#include "main.h"

void control_audio_rec_bus_init(void)
{
    if (control_audio_rec_bus_publish(0U, AUDIO_REC_BUS_ARM_OFF, 0U) == 0U)
        Error_Handler();
}

uint8_t control_audio_rec_bus_publish(uint16_t mask, audio_rec_bus_arm_t arm,
                                      uint8_t flags)
{
    flags &= (AUDIO_REC_BUS_SOURCE_LINE_DIRECT | AUDIO_REC_BUS_SOURCE_MIC_LOGICAL
              | AUDIO_REC_BUS_CAPTURE_ENABLED);
    const uint32_t packed=(uint32_t)mask|(((uint32_t)arm&3U)<<16)
        |((uint32_t)flags<<18);
    return control_rt_publish_param_now(0U, CONTROL_AUDIO_PARAM_REC_BUS,
                                         packed, 0U);
}

uint8_t control_audio_rec_bus_publish_at(uint16_t mask, audio_rec_bus_arm_t arm,
                                         uint8_t flags, uint64_t sample)
{
    flags &= (AUDIO_REC_BUS_SOURCE_LINE_DIRECT | AUDIO_REC_BUS_SOURCE_MIC_LOGICAL
              | AUDIO_REC_BUS_CAPTURE_ENABLED);
    const uint32_t packed=(uint32_t)mask|(((uint32_t)arm&3U)<<16)
        |((uint32_t)flags<<18);
    return control_rt_publish_param(0U,CONTROL_AUDIO_PARAM_REC_BUS,packed,0U,sample);
}
