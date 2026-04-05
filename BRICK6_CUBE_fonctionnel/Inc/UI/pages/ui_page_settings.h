#ifndef UI_PAGE_SETTINGS_H
#define UI_PAGE_SETTINGS_H

#include "ui_page_manager.h"

extern const ui_page_t g_ui_page_settings;

void ui_page_settings_open(uint8_t return_page_id);
uint8_t ui_page_settings_is_open(void);
void ui_page_settings_handle_encoder(int16_t delta);
uint8_t ui_page_settings_handle_event(const ui_event_t *ev);

#endif
