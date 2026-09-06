#ifndef UI_CORE_FEEDBACK_H
#define UI_CORE_FEEDBACK_H

#include <stdint.h>

void ui_core_feedback_init(void);
void ui_core_feedback_set(const char *message, uint32_t now_ms);
uint8_t ui_core_feedback_next_deadline(uint32_t now_ms, uint32_t *out_deadline_ms);
void ui_core_feedback_service(uint32_t now_ms);
uint8_t ui_core_feedback_try_get_for_track(uint8_t active_track,
                                           uint8_t track,
                                           uint32_t now_ms,
                                           char *out,
                                           uint32_t out_len);

#endif /* UI_CORE_FEEDBACK_H */
