#ifndef BRICK_KEYBOARD_ENGINE_H
#define BRICK_KEYBOARD_ENGINE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void keyboard_engine_note_on(uint8_t note, uint8_t velocity);
void keyboard_engine_note_off(uint8_t note);
void keyboard_engine_all_notes_off(void);
void keyboard_engine_midi_receive(const uint8_t *msg, size_t len);

#ifdef __cplusplus
}
#endif

#endif
