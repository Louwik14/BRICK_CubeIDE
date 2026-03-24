#ifndef BRICK_KEYBOARD_RUNTIME_H
#define BRICK_KEYBOARD_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

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
void keyboard_runtime_set_arp_hold(bool enabled);
void keyboard_runtime_set_arp_rate(uint8_t value);
void keyboard_runtime_set_arp_oct(uint8_t value);
void keyboard_runtime_set_arp_pattern(uint8_t value);
void keyboard_runtime_set_arp_gate(uint8_t value);
void keyboard_runtime_set_arp_swing(uint8_t value);
void keyboard_runtime_set_arp_accent(uint8_t value);
void keyboard_runtime_set_arp_vel_acc(uint8_t value);
void keyboard_runtime_set_arp_strum(uint8_t value);
void keyboard_runtime_set_arp_offset(int8_t value);
void keyboard_runtime_set_arp_transpose(int8_t value);
void keyboard_runtime_set_arp_spread(uint8_t value);
void keyboard_runtime_set_arp_dir(uint8_t value);
void keyboard_runtime_set_arp_sync(uint8_t value);
void keyboard_runtime_step_octave(int8_t delta);
void keyboard_runtime_process_hall(uint8_t hall_index, bool pressed, uint8_t velocity);
void keyboard_runtime_all_notes_off(void);
void keyboard_runtime_on_active_track_changed(void);
void keyboard_runtime_on_hall_mode_changed(ui_hall_mode_t previous_mode, ui_hall_mode_t new_mode);
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
