#include "Core/engine_lane_authority.h"

#include <stddef.h>
#include <string.h>

#include "Core/track_topology.h"

void engine_lane_authority_count(const ui_track_config_t *configs,
                                 uint8_t config_count,
                                 engine_lane_usage_t *out_usage)
{
    if (out_usage == NULL)
    {
        return;
    }

    memset(out_usage, 0, sizeof(*out_usage));
    if (configs == NULL)
    {
        return;
    }

    if (config_count > TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY)
    {
        config_count = TRACK_TOPOLOGY_STORAGE_TRACK_CAPACITY;
    }

    for (uint8_t track = 0U; track < config_count; ++track)
    {
        if (track_topology_is_active(track) == 0U)
        {
            continue;
        }

        switch (configs[track].family)
        {
            case UI_TRACK_FAMILY_SYNTH:
                out_usage->synth_tracks++;
                break;

            case UI_TRACK_FAMILY_SAMPLER:
                out_usage->sampler_tracks++;
                break;

            case UI_TRACK_FAMILY_DRUM:
                out_usage->drum_tracks++;
                break;

            default:
                break;
        }
    }

    out_usage->total_tracks = (uint8_t)(out_usage->synth_tracks
            + out_usage->sampler_tracks
            + out_usage->drum_tracks);
}
