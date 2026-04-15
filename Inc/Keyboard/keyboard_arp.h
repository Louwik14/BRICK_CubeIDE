#ifndef BRICK_KEYBOARD_ARP_H
#define BRICK_KEYBOARD_ARP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void keyboard_arp_init(void);
void keyboard_arp_tick(void);
void keyboard_arp_note_on(uint8_t note, uint8_t velocity);
void keyboard_arp_note_off(uint8_t note);
void keyboard_arp_all_notes_off(void);

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

void keyboard_arp_on_mode_enter(void);
void keyboard_arp_on_mode_enter_silent(void);
void keyboard_arp_on_mode_leave(void);
void keyboard_arp_on_mode_leave_silent(void);
void keyboard_arp_clear_state_silent(void);

#ifdef __cplusplus
}
#endif

#endif
