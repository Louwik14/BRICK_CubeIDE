#ifndef UI_SAMPLE_PLAYHEAD_H
#define UI_SAMPLE_PLAYHEAD_H

#include <stdint.h>

#include "Track/entity_types.h"

typedef struct
{
    uint8_t active;
    uint32_t frame;
    uint32_t frame_count;
    uint32_t region_begin;
    uint32_t region_end;
    uint32_t loop_begin;
    uint8_t slice_count;
    uint8_t slice_index;
} ui_sample_playhead_view_t;

ui_sample_playhead_view_t ui_sample_playhead_view(
    brick_entity_id_t entity_id,
    uint16_t global_slot,
    uint32_t generation,
    uint32_t frame_count);

#endif /* UI_SAMPLE_PLAYHEAD_H */
