#ifndef UI_CORE_SEQ_TRANSPORT_H
#define UI_CORE_SEQ_TRANSPORT_H

#include <stdint.h>

#include "ui_core.h"
#include "ui_event.h"

typedef void (*ui_core_seq_transport_feedback_fn)(const char *message);
typedef void (*ui_core_seq_transport_pattern_enter_fn)(ui_pattern_mode_t mode);

uint8_t ui_core_seq_transport_handle_transport_event(const ui_event_t *ev,
                                                     uint8_t mute_active,
                                                     uint8_t shift_down,
                                                     uint8_t track_select_armed,
                                                     ui_core_seq_transport_pattern_enter_fn pattern_enter,
                                                     ui_core_seq_transport_feedback_fn feedback);
uint8_t ui_core_seq_transport_handle_seq_mode_event(const ui_event_t *ev,
                                                    ui_hall_mode_t hall_mode,
                                                    uint8_t shift_down,
                                                    ui_core_seq_transport_feedback_fn feedback);

#endif /* UI_CORE_SEQ_TRANSPORT_H */
