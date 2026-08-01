#ifndef UI_PAGE_TEMPLATE_FILTER_H
#define UI_PAGE_TEMPLATE_FILTER_H

#include "ui_page.h"

extern const ui_page_t g_ui_page_template_env;

void ui_page_template_env_register_families(void);
void ui_page_template_env_open_primary(void);
uint8_t ui_page_template_env_open_vca(void);
void ui_page_template_env_toggle_subset(void);

#endif /* UI_PAGE_TEMPLATE_FILTER_H */
