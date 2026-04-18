#ifndef UI_CORE_CLIPBOARD_H
#define UI_CORE_CLIPBOARD_H

#include <stdint.h>

#include "ui_event.h"

typedef void (*ui_core_clipboard_feedback_fn)(const char *message);

void ui_core_clipboard_init(void);
uint8_t ui_core_clipboard_handle_track_event(const ui_event_t *ev,
                                             uint8_t track_select_armed,
                                             uint8_t shift_down,
                                             ui_core_clipboard_feedback_fn feedback);
uint8_t ui_core_clipboard_handle_ensemble_event(const ui_event_t *ev,
                                                uint8_t shift_down,
                                                ui_core_clipboard_feedback_fn feedback);
uint8_t ui_core_clipboard_handle_page_event(const ui_event_t *ev,
                                            uint8_t shift_down,
                                            ui_core_clipboard_feedback_fn feedback);
uint8_t ui_core_clipboard_handle_seq_track_event(const ui_event_t *ev,
                                                 uint8_t track_select_armed,
                                                 uint8_t shift_down,
                                                 ui_core_clipboard_feedback_fn feedback);

#endif /* UI_CORE_CLIPBOARD_H */
