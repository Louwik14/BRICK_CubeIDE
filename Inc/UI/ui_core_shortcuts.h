#ifndef UI_CORE_SHORTCUTS_H
#define UI_CORE_SHORTCUTS_H

#include <stdint.h>

#include "ui_event.h"

typedef uint8_t (*ui_core_shortcuts_undo_fn)(void);
typedef void (*ui_core_shortcuts_feedback_fn)(const char *message);

uint8_t ui_core_shortcuts_handle_global_event(const ui_event_t *ev,
                                              uint8_t shift_down,
                                              uint8_t track_select_armed,
                                              uint8_t mute_active,
                                              ui_core_shortcuts_undo_fn undo_request,
                                              ui_core_shortcuts_feedback_fn feedback);

#endif /* UI_CORE_SHORTCUTS_H */
