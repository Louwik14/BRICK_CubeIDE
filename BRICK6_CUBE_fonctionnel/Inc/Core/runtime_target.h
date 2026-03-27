#pragma once

#include <stdint.h>

#include "ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t ui_track;
    uint8_t has_filter_target;
    uint8_t filter_target_track;
    uint8_t has_synth_target;
    uint8_t synth_target_id;
    uint8_t midi_channel;
} runtime_target_t;

static inline uint8_t runtime_target_resolve_for_ui_track(uint8_t ui_track, runtime_target_t *out_target)
{
    if ((out_target == 0) || (ui_track >= UI_TRACK_COUNT))
    {
        return 0U;
    }

    const ui_track_config_t config = ui_get_track_config(ui_track);

    out_target->ui_track = ui_track;
    out_target->has_filter_target = 0U;
    out_target->filter_target_track = 0U;
    out_target->has_synth_target = 0U;
    out_target->synth_target_id = 0U;
    out_target->midi_channel = (uint8_t)(ui_track & 0x0FU);

    switch (config.family)
    {
        case UI_TRACK_FAMILY_INPUT1:
            out_target->has_filter_target = 1U;
            out_target->filter_target_track = 0U;
            break;

        case UI_TRACK_FAMILY_INPUT2:
            out_target->has_filter_target = 1U;
            out_target->filter_target_track = 1U;
            break;

        case UI_TRACK_FAMILY_INPUT3:
            out_target->has_filter_target = 1U;
            out_target->filter_target_track = 2U;
            break;

        case UI_TRACK_FAMILY_SYNTH:
            out_target->has_synth_target = 1U;
            out_target->synth_target_id = 0U; /* moteur synth global actuel */
            if (config.type != UI_TRACK_TYPE_MONOB)
            {
                out_target->has_filter_target = 1U;
                out_target->filter_target_track = 3U;
            }
            break;

        default:
            break;
    }

    return 1U;
}

static inline uint8_t runtime_target_resolve_filter_for_ui_track(uint8_t ui_track, uint8_t *out_filter_track)
{
    if (out_filter_track == 0)
    {
        return 0U;
    }

    runtime_target_t target;
    if (runtime_target_resolve_for_ui_track(ui_track, &target) == 0U)
    {
        return 0U;
    }

    if (target.has_filter_target == 0U)
    {
        return 0U;
    }

    *out_filter_track = target.filter_target_track;
    return 1U;
}

#ifdef __cplusplus
}
#endif
