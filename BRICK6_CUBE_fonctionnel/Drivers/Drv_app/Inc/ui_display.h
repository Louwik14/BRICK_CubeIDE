#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include "../../csrc/u8g2.h"
void ui_display_init(void);
void ui_display_begin_frame(void);
void ui_display_end_frame(void);
void ui_display_set_font_default(void);
u8g2_t *ui_display_get_u8g2(void);

#endif /* UI_DISPLAY_H */
