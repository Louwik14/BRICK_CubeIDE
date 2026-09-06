#ifndef UI_RENDERER_OLED_H
#define UI_RENDERER_OLED_H

#include <stdint.h>

void ui_renderer_oled_draw(void);
void ui_renderer_oled_service_render(void);
void ui_renderer_oled_service_deadline(void);
uint32_t ui_renderer_oled_next_render_wait_ticks(void);
uint8_t ui_renderer_oled_is_rendering(void);

#endif /* UI_RENDERER_OLED_H */
