#ifndef UI_PAGE_TEMPLATE_SEQ_H
#define UI_PAGE_TEMPLATE_SEQ_H

#include "ui_page.h"

void ui_page_template_seq_register_families(void);
uint8_t ui_page_template_seq_handle_encoder(uint8_t encoder, int16_t delta);

extern const ui_page_t g_ui_page_template_seq;

#endif /* UI_PAGE_TEMPLATE_SEQ_H */
