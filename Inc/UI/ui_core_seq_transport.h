#ifndef UI_CORE_SEQ_TRANSPORT_H
#define UI_CORE_SEQ_TRANSPORT_H

#include <stdint.h>

#include "ui_core.h"
#include "ui_event.h"

typedef void (*ui_core_seq_transport_feedback_fn)(const char *message);
uint8_t ui_core_seq_transport_handle_seq_mode_event(const ui_event_t *ev,
                                                    ui_hall_mode_t hall_mode,
                                                    uint8_t shift_down,
                                                    ui_core_seq_transport_feedback_fn feedback);

#endif /* UI_CORE_SEQ_TRANSPORT_H */
