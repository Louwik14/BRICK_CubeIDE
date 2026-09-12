#ifndef CONTROL_MUSIC_PUBLICATION_H
#define CONTROL_MUSIC_PUBLICATION_H

#include <stdint.h>
#include "Track/control_music_output.h"
#include "IPC/control_music_capacity.h"
#include "Seq/seq_capacity_contract.h"

_Static_assert(CONTROL_MUSIC_SOURCE_MAX_HORIZON_BURST
                   == SEQ_PRODUCT_MAX_MUSIC_ACTIONS_PER_HORIZON,
               "source music reserve must cover the sequencer horizon");
_Static_assert(CONTROL_MUSIC_NOTE_FX_MAX_TEMPORAL_OUTPUTS_PER_HORIZON
                   == SEQ_PRODUCT_MAX_EMITTING_VOICES,
               "temporal Note FX reserve must cover product polyphony");
_Static_assert(CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST
                   >= CONTROL_MUSIC_SOURCE_MAX_HORIZON_BURST,
               "internal music staging must cover every producer");

uint16_t control_music_publication_free(void);
uint8_t control_music_publication_publish_merged_window(
    const control_music_transition_t *internal_actions,
    const uint16_t *internal_next, const uint16_t *internal_heads,
    const uint32_t *internal_order,
    uint16_t internal_count,
    const control_music_transition_t *external_actions,
    const uint16_t *external_next, const uint16_t *external_heads,
    const uint32_t *external_order,
    uint16_t external_count, uint16_t bucket_count);

#endif
