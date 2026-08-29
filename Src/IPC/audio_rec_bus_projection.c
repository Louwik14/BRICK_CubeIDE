#include "IPC/audio_rec_bus_projection.h"

#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "Core/live_clock.h"

static audio_rec_bus_control_snapshot_t g_audio_rec_bus_snapshot;

void audio_rec_bus_projection_control_init(void)
{
    (void)audio_rec_bus_projection_control_publish(
        0U, AUDIO_REC_BUS_ARM_OFF, 0U);
}

uint8_t audio_rec_bus_projection_control_publish(uint16_t source_entity_mask,
                                                 audio_rec_bus_arm_t arm,
                                                 uint8_t source_flags)
{
    uint64_t sample_time = 0U;
    const uint32_t flags = (uint8_t)(source_flags
        & (AUDIO_REC_BUS_SOURCE_LINE_DIRECT
            | AUDIO_REC_BUS_SOURCE_MIC_LOGICAL
            | AUDIO_REC_BUS_CAPTURE_ENABLED));
    const uint32_t packed = source_entity_mask
        | (((uint32_t)arm & 3U) << 16) | (flags << 18);
    if (!live_clock_read_audio_sample(&sample_time))
        return 0U;
    return control_audio_publish_param(0U, CONTROL_AUDIO_PARAM_REC_BUS,
                                       packed, 0U, sample_time);
}

uint8_t audio_rec_bus_projection_audio_apply(uint32_t packed)
{
    ++g_audio_rec_bus_snapshot.generation;
    g_audio_rec_bus_snapshot.source_entity_mask = (uint16_t)packed;
    g_audio_rec_bus_snapshot.arm = (uint8_t)((packed >> 16) & 3U);
    g_audio_rec_bus_snapshot.source_flags = (uint8_t)((packed >> 18) & 7U);
    return 1U;
}

void audio_rec_bus_projection_audio_init(void)
{
    g_audio_rec_bus_snapshot = (audio_rec_bus_control_snapshot_t){0};
}

uint8_t audio_rec_bus_projection_audio_read(
    audio_rec_bus_control_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0)
    {
        return 0U;
    }

    *out_snapshot = g_audio_rec_bus_snapshot;
    return (uint8_t)(g_audio_rec_bus_snapshot.generation != 0U);
}
