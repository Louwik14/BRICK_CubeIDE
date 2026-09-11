#ifndef UI_SAMPLER_PLAYHEAD_H
#define UI_SAMPLER_PLAYHEAD_H

#include <stdint.h>

#include "Track/entity_types.h"

typedef struct
{
    uint8_t active;
    float normalized_position;
} ui_sampler_playhead_view_t;

ui_sampler_playhead_view_t ui_sampler_playhead_view(
    brick_entity_id_t entity_id,
    uint16_t global_slot);

#endif /* UI_SAMPLER_PLAYHEAD_H */
