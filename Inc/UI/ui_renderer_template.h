#ifndef UI_RENDERER_TEMPLATE_H
#define UI_RENDERER_TEMPLATE_H

#include <stdint.h>

#include "ui_template_page.h"

void ui_format_param_127_00(float value, float min_value, float max_value, char *out, uint32_t out_len);
uint8_t ui_renderer_template_draw_step(const ui_template_page_state_t *state);
uint8_t ui_renderer_template_draw_pending(void);
void ui_renderer_template_draw_cancel(void);

#endif /* UI_RENDERER_TEMPLATE_H */
