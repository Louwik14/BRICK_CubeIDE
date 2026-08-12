#pragma once

#include <stdint.h>

#include "audio_float.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Physical input lanes are block-local audio authorities.  They are kept
 * separate from StereoTrack: tracks[0] remains available for its historical
 * DSP/output roles until the later EXT migration.
 */
typedef struct
{
    float left[AUDIO_BLOCK_SIZE];
    float right[AUDIO_BLOCK_SIZE];
} audio_physical_line_lane_t;

typedef struct
{
    float mono[AUDIO_BLOCK_SIZE];
} audio_physical_mic_lane_t;

typedef struct
{
    audio_physical_line_lane_t line;
    audio_physical_mic_lane_t mic;
} audio_physical_inputs_t;

#ifdef __cplusplus
}
#endif
