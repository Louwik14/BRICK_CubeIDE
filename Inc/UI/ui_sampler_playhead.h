#ifndef UI_SAMPLER_PLAYHEAD_H
#define UI_SAMPLER_PLAYHEAD_H

#include <stdint.h>

#include "Track/entity_topology.h"

typedef struct
{
    uint8_t active;
    float normalized_position;
} ui_sampler_playhead_view_t;

/* CONTROL-side notification emitted only after the logical note event has
 * been accepted for AUDIO projection. */
void ui_sampler_playhead_note_trigger(brick_entity_id_t entity_id,
                                      uint64_t due_sample);

ui_sampler_playhead_view_t ui_sampler_playhead_view(
    brick_entity_id_t entity_id,
    uint64_t now_sample,
    uint32_t duration_samples,
    uint8_t mode);

#endif /* UI_SAMPLER_PLAYHEAD_H */
