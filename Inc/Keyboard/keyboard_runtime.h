#ifndef BRICK_KEYBOARD_RUNTIME_H
#define BRICK_KEYBOARD_RUNTIME_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "Seq/seq_types.h"
#include "Keyboard/ui_keyboard_app.h"
#include "UI/ui_core.h"

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
void keyboard_runtime_set_mono_last(bool enabled);
void keyboard_runtime_step_octave(int8_t delta);
void keyboard_runtime_process_midi(const uint8_t *msg, size_t len, seq_clock_src_t source);
void keyboard_runtime_process_hall(uint8_t hall_index, bool pressed, uint8_t velocity);
void keyboard_runtime_all_notes_off(void);
void keyboard_runtime_sync_track_focus_context(void);
void keyboard_runtime_on_hall_mode_changed(ui_hall_mode_t previous_mode, ui_hall_mode_t new_mode);
uint8_t keyboard_runtime_active_track_is_plain_input_audio(void);
uint8_t keyboard_runtime_get_root_index(void);
uint8_t keyboard_runtime_get_scale_index(void);
bool keyboard_runtime_get_omnichord(void);
note_order_t keyboard_runtime_get_note_order(void);
bool keyboard_runtime_get_chord_override(void);
bool keyboard_runtime_get_mono_last(void);
int8_t keyboard_runtime_get_octave_shift(void);
void keyboard_runtime_get_active_chord_label(char *out, uint32_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* BRICK_KEYBOARD_RUNTIME_H */
