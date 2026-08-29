#ifndef CONTROL_MUSIC_PUBLICATION_H
#define CONTROL_MUSIC_PUBLICATION_H

#include <stdint.h>
#include "Track/control_music_output.h"

#define CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST 233U
#define CONTROL_MUSIC_EXTERNAL_STAGING_CAPACITY 128U

uint16_t control_music_publication_free(void);
uint8_t control_music_publication_publish_merged_window(
    const control_music_action_t *internal_actions,
    const uint16_t *internal_next, const uint16_t *internal_heads,
    uint16_t internal_count,
    const control_music_action_t *external_actions,
    const uint16_t *external_next, const uint16_t *external_heads,
    uint16_t external_count, uint16_t bucket_count);

#endif
