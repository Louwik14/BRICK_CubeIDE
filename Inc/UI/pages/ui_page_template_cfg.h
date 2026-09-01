#ifndef UI_PAGE_TEMPLATE_CFG_H
#define UI_PAGE_TEMPLATE_CFG_H

#include "ui_page.h"

extern const ui_page_t g_ui_page_template_cfg;
extern const ui_page_t g_ui_page_template_rec_cfg;

void ui_page_template_cfg_register_families(void);
void ui_page_template_rec_cfg_open_main(void);
uint8_t ui_page_template_rec_cfg_handle_encoder(uint8_t encoder, int16_t delta);
uint8_t ui_page_template_cfg_handle_encoder(uint8_t encoder, int16_t delta);

#endif /* UI_PAGE_TEMPLATE_CFG_H */
