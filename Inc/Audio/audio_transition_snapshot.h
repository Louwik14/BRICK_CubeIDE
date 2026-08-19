#ifndef AUDIO_TRANSITION_SNAPSHOT_H
#define AUDIO_TRANSITION_SNAPSHOT_H

#include <stdint.h>

#include "Seq/seq_types.h"

void audio_transition_snapshot_init(void);
void audio_transition_snapshot_publish(uint8_t global_active,
                                       const uint8_t *track_active);
uint8_t audio_transition_snapshot_read_all(uint8_t *out_global_active,
                                           uint8_t *out_track_active,
                                           uint8_t capacity);

#endif /* AUDIO_TRANSITION_SNAPSHOT_H */
