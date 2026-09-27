#pragma once

#include <stdint.h>

#include "Sampler/multi_sample_index.h"

typedef struct
{
    const char *filename;
    uint8_t smpl_root_valid;
    uint8_t smpl_root;
    uint8_t inst_root_valid;
    uint8_t inst_root;
    uint8_t inst_velocity_valid;
    uint8_t inst_vel_low;
    uint8_t inst_vel_high;
} multi_sample_zone_observation_t;

typedef struct
{
    uint8_t root_note;
    uint8_t vel_low;
    uint8_t vel_high;
    uint8_t metadata_flags;
    uint8_t velocity_center_valid;
    uint8_t velocity_center;
    uint8_t skip_variant;
} multi_sample_zone_resolution_t;

typedef enum
{
    MULTI_SAMPLE_ZONE_DETECT_OK = 0,
    MULTI_SAMPLE_ZONE_DETECT_INVALID,
    MULTI_SAMPLE_ZONE_DETECT_OVERFLOW
} multi_sample_zone_detect_result_t;

multi_sample_zone_detect_result_t multi_sample_zone_detect_folder(
    const multi_sample_zone_observation_t *observations,
    uint16_t count,
    multi_sample_zone_resolution_t *resolutions);
