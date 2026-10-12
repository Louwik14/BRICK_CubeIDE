#ifndef UI_PAGE_TEMPLATE_PLAY_H
#define UI_PAGE_TEMPLATE_PLAY_H

#include <stdint.h>

#include "ui_page.h"
#include "ui_template_page.h"

void ui_page_template_play_register_families(void);
void ui_page_template_play_open_primary(void);
void ui_page_template_play_toggle_subset(void);
uint8_t ui_page_template_play_handle_encoder(uint8_t encoder, int16_t delta);
const ui_template_family_t *ui_page_template_play_resolve_for_track(uint8_t track);
extern const ui_page_t g_ui_page_template_play;

#endif /* UI_PAGE_TEMPLATE_PLAY_H */
