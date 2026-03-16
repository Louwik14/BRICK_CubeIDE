#ifndef UI_RENDERER_OLED_H
#define UI_RENDERER_OLED_H

#include "../../Drivers/csrc/u8g2.h"

extern u8g2_t g_u8g2;

void ui_renderer_oled_init(void);
void ui_renderer_oled_draw(void);

#endif /* UI_RENDERER_OLED_H */
