#ifndef UNDO_V1_H
#define UNDO_V1_H

#include <stdint.h>

void undo_v1_init(void);
void undo_v1_clear_history(void);
void undo_v1_begin_gesture(uint32_t gesture_key);
uint8_t undo_v1_capture_before_edit(uint8_t source_hint);
uint8_t undo_v1_restore(uint8_t resume_transport);
uint8_t undo_v1_is_available(void);

#endif
