#pragma once

#include <stdint.h>

#include "App/control_domain.h"
#include "Seq/seq_edit.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t control_clipboard_ui_available(void);
uint8_t control_clipboard_request_apply(control_clipboard_operation_t operation,
                                        uint8_t target,
                                        uint8_t arg0,
                                        uint8_t arg1);
uint8_t control_clipboard_request_sequence_apply(uint8_t track,
                                                 const seq_step_id_t *steps,
                                                 uint8_t count,
                                                 uint8_t clear);
void control_clipboard_process(const control_clipboard_intent_t *intent);

#ifdef __cplusplus
}
#endif
