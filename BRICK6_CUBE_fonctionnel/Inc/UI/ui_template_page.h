#ifndef UI_TEMPLATE_PAGE_H
#define UI_TEMPLATE_PAGE_H

#include <stdint.h>

#include "ui_page.h"
#include "ui_param.h"

typedef struct
{
    const char *title;
    ui_param_bank_t param_bank;
} ui_template_subpage_t;

typedef struct
{
    const char *family_title;
    const char *nav_labels[4];
    ui_template_subpage_t subpages[4];
    uint8_t default_subpage;
} ui_template_family_t;

typedef struct
{
    const ui_template_family_t *family;
    uint8_t active_subpage;
    uint8_t has_visited;
} ui_template_page_state_t;

void ui_template_page_enter(void);
void ui_template_page_leave(void);
void ui_template_page_handle_event(const ui_event_t *ev);
void ui_template_page_tick(void);
void ui_template_page_render(void);

void ui_template_page_select_subpage(ui_template_page_state_t *state, uint8_t subpage_index);
const ui_template_subpage_t *ui_template_page_get_active_subpage(const ui_template_page_state_t *state);

#endif /* UI_TEMPLATE_PAGE_H */
