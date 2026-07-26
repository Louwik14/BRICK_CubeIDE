#ifndef UI_PAGE_TEMPLATE_FILTER_H
#define UI_PAGE_TEMPLATE_FILTER_H

#include "ui_page.h"

extern const ui_page_t g_ui_page_template_colors;
extern const ui_page_t g_ui_page_template_vca;

void ui_page_template_colors_register_families(void);
void ui_page_template_vca_register_families(void);
void ui_page_template_colors_open_primary(void);
void ui_page_template_colors_toggle_subset(void);

#endif /* UI_PAGE_TEMPLATE_FILTER_H */
