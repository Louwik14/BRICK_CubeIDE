#ifndef AUDIO_NOTE_ADMISSION_H
#define AUDIO_NOTE_ADMISSION_H

#include <stdint.h>

#include "Audio/control_audio_queue.h"

void audio_note_admission_init(void);
uint8_t audio_note_admission_apply(const control_audio_event_t *event);
void audio_note_admission_close_entity(brick_entity_id_t entity_id);
void audio_note_admission_close_all(void);

#endif /* AUDIO_NOTE_ADMISSION_H */
