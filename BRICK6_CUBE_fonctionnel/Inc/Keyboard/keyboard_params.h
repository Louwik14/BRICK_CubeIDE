#ifndef BRICK_KEYBOARD_PARAMS_H
#define BRICK_KEYBOARD_PARAMS_H

#include <stdbool.h>
#include <stdint.h>

#include "Keyboard/ui_keyboard_app.h"

#ifdef __cplusplus
extern "C" {
#endif

void keyboard_params_init(void);

void keyboard_params_set_root(uint8_t root_index);
void keyboard_params_set_scale(uint8_t scale_index);
void keyboard_params_set_omnichord(bool enabled);
void keyboard_params_set_note_order(note_order_t order);
void keyboard_params_set_chord_override(bool enabled);

uint8_t keyboard_params_get_root_index(void);
uint8_t keyboard_params_get_scale_index(void);
bool keyboard_params_get_omnichord(void);
note_order_t keyboard_params_get_note_order(void);
bool keyboard_params_get_chord_override(void);

#ifdef __cplusplus
}
#endif

#endif
