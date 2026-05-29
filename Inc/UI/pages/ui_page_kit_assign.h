#ifndef UI_PAGE_KIT_ASSIGN_H
#define UI_PAGE_KIT_ASSIGN_H

#include <stdint.h>

#include "ui_page.h"

extern const ui_page_t g_ui_page_kit_assign;

void ui_page_kit_assign_open(void);
uint8_t ui_page_kit_assign_handle_encoder(uint8_t encoder, int16_t delta);
uint8_t ui_page_kit_assign_is_open(void);

#endif /* UI_PAGE_KIT_ASSIGN_H */
