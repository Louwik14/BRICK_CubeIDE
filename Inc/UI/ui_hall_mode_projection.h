#ifndef UI_HALL_MODE_PROJECTION_H
#define UI_HALL_MODE_PROJECTION_H

#include "ui_core.h"

typedef enum
{
    UI_HALL_ROUT_CONTEXT_NONE = 0,
    UI_HALL_ROUT_CONTEXT_SAMPLER_LOOPER
} ui_hall_rout_context_t;

ui_hall_rout_context_t ui_hall_mode_resolve_rout_context(uint8_t track, ui_hall_mode_t raw_mode);
ui_hall_mode_effective_view_t ui_hall_mode_resolve_effective_view(uint8_t track, ui_hall_mode_t raw_mode);
uint8_t ui_hall_allows_injection(uint8_t track, ui_hall_mode_t raw_mode);
uint8_t ui_hall_is_seq_context(ui_hall_mode_t raw_mode);
void ui_hall_patch_feedback_begin(uint32_t now_ms);
void ui_hall_patch_feedback_end(uint32_t now_ms);
void ui_hall_kit_feedback_begin(uint32_t now_ms);
void ui_hall_kit_feedback_end(uint32_t now_ms);
const char *ui_get_hall_mode_short_label(void);
const char *ui_get_hall_mode_suffix_label(void);

#endif /* UI_HALL_MODE_PROJECTION_H */
