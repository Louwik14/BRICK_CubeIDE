#ifndef BRICK_KEYBOARD_RUNTIME_H
#define BRICK_KEYBOARD_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "Keyboard/ui_keyboard_app.h"

#ifdef __cplusplus
extern "C" {
#endif

void keyboard_runtime_init(void);
void keyboard_runtime_tick(void);
void keyboard_runtime_set_root(uint8_t root_index);
void keyboard_runtime_set_scale(uint8_t scale_index);
void keyboard_runtime_set_omnichord(bool enabled);
void keyboard_runtime_set_note_order(note_order_t order);
void keyboard_runtime_set_chord_override(bool enabled);
void keyboard_runtime_step_octave(int8_t delta);
void keyboard_runtime_process_hall(uint8_t hall_index, bool pressed, uint8_t velocity);
void keyboard_runtime_all_notes_off(void);
void keyboard_runtime_on_active_track_changed(void);
uint8_t keyboard_runtime_get_root_index(void);
uint8_t keyboard_runtime_get_scale_index(void);
bool keyboard_runtime_get_omnichord(void);
note_order_t keyboard_runtime_get_note_order(void);
bool keyboard_runtime_get_chord_override(void);
int8_t keyboard_runtime_get_octave_shift(void);

#ifdef __cplusplus
}
#endif

#endif /* BRICK_KEYBOARD_RUNTIME_H */
