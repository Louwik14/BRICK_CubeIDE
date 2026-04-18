#ifndef UI_CORE_PATTERN_H
#define UI_CORE_PATTERN_H

#include <stdint.h>

#include "ui_core.h"
#include "ui_event.h"

typedef void (*ui_core_pattern_set_hall_mode_fn)(ui_hall_mode_t mode);
typedef void (*ui_core_pattern_feedback_fn)(const char *message);

void ui_core_pattern_init(void);
void ui_core_pattern_abort(void);
void ui_core_pattern_enter(ui_pattern_mode_t mode,
                           ui_hall_mode_t current_hall_mode,
                           ui_core_pattern_set_hall_mode_fn set_hall_mode);
uint8_t ui_core_pattern_handle_mode_event(const ui_event_t *ev,
                                          ui_hall_mode_t hall_mode,
                                          uint8_t shift_down,
                                          uint8_t track_select_armed,
                                          ui_core_pattern_set_hall_mode_fn set_hall_mode,
                                          ui_core_pattern_feedback_fn feedback);
ui_pattern_substate_t ui_core_pattern_get_substate(void);
uint8_t ui_core_pattern_get_selected_bank(void);
ui_pattern_mode_t ui_core_pattern_get_mode(void);

#endif /* UI_CORE_PATTERN_H */
