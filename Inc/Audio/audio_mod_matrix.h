#ifndef AUDIO_MOD_MATRIX_H
#define AUDIO_MOD_MATRIX_H

#include "Audio/control_audio_queue.h"

/* AUDIO consumer for the autonomous Mod Matrix configuration snapshot. */
void audio_mod_matrix_apply_snapshot(const control_audio_event_t *event);
void audio_mod_matrix_rebuild_track(uint8_t track);

#endif /* AUDIO_MOD_MATRIX_H */
