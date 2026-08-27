#ifndef AUDIO_MUSIC_ACTION_EXECUTOR_H
#define AUDIO_MUSIC_ACTION_EXECUTOR_H

#include <stdint.h>

#include "Audio/control_music_queue.h"

/* AUDIO-side invariant guard and physical executor. No musical admission or
 * occurrence ledger lives here. */
uint8_t audio_music_action_execute(const control_music_action_t *action);
void audio_music_action_force_close_all(void);

#endif /* AUDIO_MUSIC_ACTION_EXECUTOR_H */
