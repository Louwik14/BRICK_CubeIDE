#ifndef UI_PAGE_TEMPLATE_MOD_H
#define UI_PAGE_TEMPLATE_MOD_H

#include <stdint.h>

#include "ui_page.h"
#include "Param/param_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const ui_page_t g_ui_page_template_mod;

typedef struct
{
    char widget_line1[5];
    char widget_line2[5];
    char tweak_label[9];
    param_id_t param;
} ui_mod_destination_projection_t;

uint8_t ui_page_template_mod_project_destination(
    uint8_t track, uint16_t index, ui_mod_destination_projection_t *out);

void ui_page_template_mod_register_families(void);
void ui_page_template_mod_open_primary(void);
void ui_page_template_mod_toggle_subset(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_PAGE_TEMPLATE_MOD_H */
