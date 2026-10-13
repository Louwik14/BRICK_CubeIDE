#ifndef UI_MACRO_INTERACTION_H
#define UI_MACRO_INTERACTION_H

#include <stdint.h>
#include "Param/param_ids.h"

typedef enum {
    UI_MACRO_LIVE_PRESSURE = 0,
    UI_MACRO_LIVE_TOGGLE,
    UI_MACRO_ASSIGN
} ui_macro_mode_t;

void ui_macro_interaction_init(void);
void ui_macro_interaction_reset(void);
void ui_macro_interaction_enter(void);
void ui_macro_interaction_leave(void);
void ui_macro_interaction_shift_tap(uint32_t now_ms);
uint8_t ui_macro_interaction_service_navigation(uint32_t now_ms);
ui_macro_mode_t ui_macro_interaction_get_mode(void);
uint8_t ui_macro_interaction_get_held_macro(uint8_t *out_macro);
void ui_macro_interaction_note_hall_press(uint8_t hall);
void ui_macro_interaction_note_hall_release(uint8_t hall);
void ui_macro_interaction_service_hall(uint8_t hall, uint8_t pressed);
uint8_t ui_macro_interaction_get_edit_source(param_id_t param,
                                             uint8_t track,
                                             float base_value,
                                             float *out_value);
uint8_t ui_macro_interaction_commit_edit(uint8_t encoder,
                                         param_id_t param,
                                         uint8_t track,
                                         float target_value,
                                         uint8_t clear);
uint8_t ui_macro_interaction_param_is_locked(param_id_t param);
uint8_t ui_macro_interaction_get_param_lock_value(param_id_t param,
                                                  uint8_t *out_track, float *out_target_value);

#endif
