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
void keyboard_runtime_set_arp_hold_for_track(uint8_t track, bool enabled);
void keyboard_runtime_set_arp_rate_for_track(uint8_t track, uint8_t value);
void keyboard_runtime_set_arp_oct_for_track(uint8_t track, uint8_t value);
void keyboard_runtime_set_arp_pattern_for_track(uint8_t track, uint8_t value);
void keyboard_runtime_set_arp_gate_for_track(uint8_t track, uint8_t value);
void keyboard_runtime_set_arp_swing_for_track(uint8_t track, uint8_t value);
void keyboard_runtime_set_arp_accent_for_track(uint8_t track, uint8_t value);
void keyboard_runtime_set_arp_vel_acc_for_track(uint8_t track, uint8_t value);
void keyboard_runtime_set_arp_strum_for_track(uint8_t track, uint8_t value);
void keyboard_runtime_set_arp_offset_for_track(uint8_t track, int8_t value);
void keyboard_runtime_set_arp_transpose_for_track(uint8_t track, int8_t value);
void keyboard_runtime_set_arp_spread_for_track(uint8_t track, uint8_t value);
void keyboard_runtime_set_arp_dir_for_track(uint8_t track, uint8_t value);
void keyboard_runtime_set_arp_sync_for_track(uint8_t track, uint8_t value);
void keyboard_runtime_step_octave(int8_t delta);
void keyboard_runtime_process_midi(const uint8_t *msg, size_t len, seq_clock_src_t source);
void keyboard_runtime_process_hall(uint8_t hall_index, bool pressed, uint8_t velocity);
void keyboard_runtime_all_notes_off(void);
void keyboard_runtime_clear_arp_track(uint8_t track);
void keyboard_runtime_clear_arp_seq_step_source(void);
void keyboard_runtime_sync_track_focus_context(void);
void keyboard_runtime_on_hall_mode_changed(ui_hall_mode_t previous_mode, ui_hall_mode_t new_mode);
uint8_t keyboard_runtime_active_track_is_plain_input_audio(void);
uint8_t keyboard_runtime_active_track_is_input_hybrid(void);
uint8_t keyboard_runtime_get_root_index(void);
uint8_t keyboard_runtime_get_scale_index(void);
bool keyboard_runtime_get_omnichord(void);
note_order_t keyboard_runtime_get_note_order(void);
bool keyboard_runtime_get_chord_override(void);
bool keyboard_runtime_get_mono_last(void);
int8_t keyboard_runtime_get_octave_shift(void);
void keyboard_runtime_get_active_chord_label(char *out, uint32_t out_len);
bool keyboard_runtime_get_arp_hold_for_track(uint8_t track);
uint8_t keyboard_runtime_get_arp_rate_for_track(uint8_t track);
uint8_t keyboard_runtime_get_arp_oct_for_track(uint8_t track);
uint8_t keyboard_runtime_get_arp_pattern_for_track(uint8_t track);
uint8_t keyboard_runtime_get_arp_gate_for_track(uint8_t track);
uint8_t keyboard_runtime_get_arp_swing_for_track(uint8_t track);
uint8_t keyboard_runtime_get_arp_accent_for_track(uint8_t track);
uint8_t keyboard_runtime_get_arp_vel_acc_for_track(uint8_t track);
uint8_t keyboard_runtime_get_arp_strum_for_track(uint8_t track);
int8_t keyboard_runtime_get_arp_offset_for_track(uint8_t track);
int8_t keyboard_runtime_get_arp_transpose_for_track(uint8_t track);
uint8_t keyboard_runtime_get_arp_spread_for_track(uint8_t track);
uint8_t keyboard_runtime_get_arp_dir_for_track(uint8_t track);
uint8_t keyboard_runtime_get_arp_sync_for_track(uint8_t track);

#ifdef __cplusplus
}
#endif

#endif /* BRICK_KEYBOARD_RUNTIME_H */
