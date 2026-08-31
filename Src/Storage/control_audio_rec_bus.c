#include "IPC/control_audio_rec_bus.h"

#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "IPC/live_clock_control.h"

void control_audio_rec_bus_init(void)
{ (void)control_audio_rec_bus_publish(0U, AUDIO_REC_BUS_ARM_OFF, 0U); }

uint8_t control_audio_rec_bus_publish(uint16_t mask, audio_rec_bus_arm_t arm,
                                      uint8_t flags)
{
    uint64_t sample=0U;
    return live_clock_read_audio_sample(&sample)
        ? control_audio_rec_bus_publish_at(mask,arm,flags,sample) : 0U;
}

uint8_t control_audio_rec_bus_publish_at(uint16_t mask, audio_rec_bus_arm_t arm,
                                         uint8_t flags, uint64_t sample)
{
    flags &= (AUDIO_REC_BUS_SOURCE_LINE_DIRECT | AUDIO_REC_BUS_SOURCE_MIC_LOGICAL
              | AUDIO_REC_BUS_CAPTURE_ENABLED);
    const uint32_t packed=(uint32_t)mask|(((uint32_t)arm&3U)<<16)
        |((uint32_t)flags<<18);
    return control_audio_publish_param(0U,CONTROL_AUDIO_PARAM_REC_BUS,packed,0U,sample);
}
