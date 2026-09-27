#ifndef UI_PAGE_SETTINGS_H
#define UI_PAGE_SETTINGS_H

#include <stdint.h>

#include "ui_page_manager.h"

extern const ui_page_t g_ui_page_settings;

void ui_page_settings_open(uint8_t return_page_id);
void ui_page_settings_open_sample_browser(uint8_t return_page_id);
void ui_page_settings_close_to_return_page(void);
uint8_t ui_page_settings_is_open(void);
enum
{
    UI_SETTINGS_KEYBOARD_PREVIEW_NONE = 0U,
    UI_SETTINGS_KEYBOARD_PREVIEW_RAM,
    UI_SETTINGS_KEYBOARD_PREVIEW_STREAM,
    UI_SETTINGS_KEYBOARD_PREVIEW_MULTI
};
uint8_t ui_page_settings_keyboard_preview_context(void);
uint32_t ui_page_settings_keyboard_preview_note_on(uint8_t context,
                                                   uint8_t note,
                                                   uint8_t velocity);
void ui_page_settings_keyboard_preview_note_off(uint8_t context,
                                                uint32_t epoch);
void ui_page_settings_keyboard_preview_all_notes_off(void);
uint8_t ui_page_settings_multi_clear_is_active(void);
void ui_page_settings_handle_encoder(uint8_t encoder, int16_t delta);
uint8_t ui_page_settings_handle_event(const ui_event_t *ev);

#endif
