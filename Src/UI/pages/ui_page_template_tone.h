#ifndef UI_PAGE_TEMPLATE_TONE_H
#define UI_PAGE_TEMPLATE_TONE_H

#include "ui_page.h"
#include "ui_template_page.h"

extern const ui_page_t g_ui_page_template_tone;

void ui_page_template_tone_register_families(void);
void ui_page_template_tone_open_primary(void);
void ui_page_template_tone_open_global_master(void);
uint8_t ui_page_template_tone_is_global_master(void);
void ui_page_template_tone_toggle_subset(void);
const ui_template_family_t *ui_page_template_tone_resolve_for_track(uint8_t track, uint8_t scope_index);

#endif /* UI_PAGE_TEMPLATE_TONE_H */
