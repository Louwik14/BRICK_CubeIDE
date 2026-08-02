#ifndef ENGINE_LANE_AUTHORITY_H
#define ENGINE_LANE_AUTHORITY_H

#include <stdint.h>

#include "UI/ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t synth_tracks;
    uint8_t sampler_tracks;
    uint8_t drum_tracks;
    uint8_t total_tracks;
} engine_lane_usage_t;

void engine_lane_authority_count(const ui_track_config_t *configs,
                                 uint8_t config_count,
                                 engine_lane_usage_t *out_usage);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_LANE_AUTHORITY_H */
