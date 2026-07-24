#ifndef BRICK_KEYBOARD_ARP_H
#define BRICK_KEYBOARD_ARP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t note;
    uint8_t velocity;
    uint32_t on_offset_samples;
    uint32_t off_offset_samples;
} keyboard_arp_scheduled_note_t;

void keyboard_arp_init(void);
void keyboard_arp_sync_track(uint8_t track);
void keyboard_arp_tick(void);
void keyboard_arp_note_on(uint8_t note, uint8_t velocity);
void keyboard_arp_note_off(uint8_t note);
void keyboard_arp_note_on_for_track(uint8_t track, uint8_t note, uint8_t velocity);
void keyboard_arp_note_off_for_track(uint8_t track, uint8_t note);
uint8_t keyboard_arp_seq_step_render_for_track(uint8_t track,
                                               const uint8_t *notes,
                                               const uint8_t *velocities,
                                               uint8_t count,
                                               uint32_t samples_per_step_q16,
                                               keyboard_arp_scheduled_note_t *out_notes,
                                               uint8_t max_out_notes);
void keyboard_arp_clear_seq_step_source(void);
void keyboard_arp_all_notes_off_track(uint8_t track);
void keyboard_arp_all_notes_off(void);
uint8_t keyboard_arp_has_hold_activity(void);

void keyboard_arp_set_hold(bool enabled);
void keyboard_arp_set_rate(uint8_t value);
void keyboard_arp_set_oct(uint8_t value);
void keyboard_arp_set_pattern(uint8_t value);
void keyboard_arp_set_gate(uint8_t value);
void keyboard_arp_set_swing(uint8_t value);
void keyboard_arp_set_accent(uint8_t value);
void keyboard_arp_set_vel_acc(uint8_t value);
void keyboard_arp_set_strum(uint8_t value);
void keyboard_arp_set_offset(int8_t value);
void keyboard_arp_set_transpose(int8_t value);
void keyboard_arp_set_spread(uint8_t value);
void keyboard_arp_set_dir(uint8_t value);
void keyboard_arp_set_sync(uint8_t value);

void keyboard_arp_set_hold_for_track(uint8_t track, bool enabled);
void keyboard_arp_set_rate_for_track(uint8_t track, uint8_t value);
void keyboard_arp_set_oct_for_track(uint8_t track, uint8_t value);
void keyboard_arp_set_pattern_for_track(uint8_t track, uint8_t value);
void keyboard_arp_set_gate_for_track(uint8_t track, uint8_t value);
void keyboard_arp_set_swing_for_track(uint8_t track, uint8_t value);
void keyboard_arp_set_accent_for_track(uint8_t track, uint8_t value);
void keyboard_arp_set_vel_acc_for_track(uint8_t track, uint8_t value);
void keyboard_arp_set_strum_for_track(uint8_t track, uint8_t value);
void keyboard_arp_set_offset_for_track(uint8_t track, int8_t value);
void keyboard_arp_set_transpose_for_track(uint8_t track, int8_t value);
void keyboard_arp_set_spread_for_track(uint8_t track, uint8_t value);
void keyboard_arp_set_dir_for_track(uint8_t track, uint8_t value);
void keyboard_arp_set_sync_for_track(uint8_t track, uint8_t value);

bool keyboard_arp_get_hold_for_track(uint8_t track);
uint8_t keyboard_arp_get_rate_for_track(uint8_t track);
uint8_t keyboard_arp_get_oct_for_track(uint8_t track);
uint8_t keyboard_arp_get_pattern_for_track(uint8_t track);
uint8_t keyboard_arp_get_gate_for_track(uint8_t track);
uint8_t keyboard_arp_get_swing_for_track(uint8_t track);
uint8_t keyboard_arp_get_accent_for_track(uint8_t track);
uint8_t keyboard_arp_get_vel_acc_for_track(uint8_t track);
uint8_t keyboard_arp_get_strum_for_track(uint8_t track);
int8_t keyboard_arp_get_offset_for_track(uint8_t track);
int8_t keyboard_arp_get_transpose_for_track(uint8_t track);
uint8_t keyboard_arp_get_spread_for_track(uint8_t track);
uint8_t keyboard_arp_get_dir_for_track(uint8_t track);
uint8_t keyboard_arp_get_sync_for_track(uint8_t track);

void keyboard_arp_on_mode_enter(void);
void keyboard_arp_on_mode_enter_silent(void);
void keyboard_arp_on_mode_leave(void);
void keyboard_arp_on_mode_leave_silent(void);
void keyboard_arp_clear_track(uint8_t track);
void keyboard_arp_clear_state_silent(void);

#ifdef __cplusplus
}
#endif

#endif
