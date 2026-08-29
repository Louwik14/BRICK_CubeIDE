#ifndef AUDIO_REC_BUS_PROJECTION_H
#define AUDIO_REC_BUS_PROJECTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    AUDIO_REC_BUS_ARM_OFF = 0,
    AUDIO_REC_BUS_ARM_REC,
    AUDIO_REC_BUS_ARM_TRIG
} audio_rec_bus_arm_t;

enum
{
    AUDIO_REC_BUS_SOURCE_LINE_DIRECT = (1U << 0),
    AUDIO_REC_BUS_SOURCE_MIC_LOGICAL = (1U << 1),
    AUDIO_REC_BUS_CAPTURE_ENABLED = (1U << 2)
};

typedef struct
{
    uint32_t generation;
    uint16_t source_entity_mask;
    uint8_t arm;
    uint8_t source_flags;
} audio_rec_bus_control_snapshot_t;

_Static_assert(sizeof(audio_rec_bus_control_snapshot_t) == 8U,
               "AUDIO REC CONTROL snapshot ABI changed");

void audio_rec_bus_projection_control_init(void);
uint8_t audio_rec_bus_projection_control_publish(uint16_t source_entity_mask,
                                                 audio_rec_bus_arm_t arm,
                                                 uint8_t source_flags);

void audio_rec_bus_projection_audio_init(void);
uint8_t audio_rec_bus_projection_audio_apply(uint32_t packed);
uint8_t audio_rec_bus_projection_audio_read(
    audio_rec_bus_control_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_REC_BUS_PROJECTION_H */
