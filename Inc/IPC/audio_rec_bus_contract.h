#pragma once
#include <stdint.h>

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
    AUDIO_REC_BUS_CAPTURE_ENABLED = (1U << 2),
    AUDIO_REC_BUS_SOURCE_USB_DIRECT = (1U << 3)
};
