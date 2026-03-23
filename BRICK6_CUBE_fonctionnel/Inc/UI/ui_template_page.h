#ifndef UI_TEMPLATE_PAGE_H
#define UI_TEMPLATE_PAGE_H

#include <stdint.h>

#include "ui_core.h"
#include "ui_page.h"
#include "ui_param.h"
#include "ui_widgets.h"

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

typedef enum
{
    UI_TEMPLATE_FAMILY_FILTER = 0,
    UI_TEMPLATE_FAMILY_CFG,
    UI_TEMPLATE_FAMILY_COUNT
} ui_template_family_id_t;

typedef const ui_template_family_t *(*ui_template_family_resolver_fn)(void);
typedef uiw_widget_type_t (*ui_template_widget_picker_fn)(uint8_t slot,
                                                         param_id_t id,
                                                         const char *value_label,
                                                         uiw_widget_type_t suggested_widget);

typedef struct
{
    const ui_template_family_t *family;
    ui_template_family_resolver_fn family_resolver;
    ui_template_widget_picker_fn widget_picker;
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
const ui_template_family_t *ui_template_page_get_active_family(const ui_template_page_state_t *state);

void ui_template_family_registry_init(void);
void ui_template_family_register(ui_template_family_id_t family_id,
                                 ui_track_family_t track_family,
                                 ui_track_type_t track_type,
                                 const ui_template_family_t *family);
const ui_template_family_t *ui_template_family_resolve(ui_template_family_id_t family_id,
                                                       uint8_t track,
                                                       ui_track_family_t track_family,
                                                       ui_track_type_t track_type);
const ui_template_family_t *ui_template_family_resolve_active_track(ui_template_family_id_t family_id);

#endif /* UI_TEMPLATE_PAGE_H */
