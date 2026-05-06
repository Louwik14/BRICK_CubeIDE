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
    UI_TEMPLATE_FAMILY_COLORS = 0,
    UI_TEMPLATE_FAMILY_CFG,
    UI_TEMPLATE_FAMILY_TONE,
    UI_TEMPLATE_FAMILY_MOD,
    UI_TEMPLATE_FAMILY_KEYBOARD,
    UI_TEMPLATE_FAMILY_ARP,
    UI_TEMPLATE_FAMILY_SEQ,
    UI_TEMPLATE_FAMILY_MIX,
    UI_TEMPLATE_FAMILY_PLAY,
    UI_TEMPLATE_FAMILY_VCA,
    UI_TEMPLATE_FAMILY_COUNT
} ui_template_family_id_t;

typedef const ui_template_family_t *(*ui_template_family_resolver_fn)(void);
typedef uiw_widget_type_t (*ui_template_widget_picker_fn)(uint8_t slot,
                                                         param_id_t id,
                                                         const char *value_label,
                                                         uiw_widget_type_t suggested_widget);
typedef uint8_t (*ui_template_subpage_enabled_fn)(uint8_t subpage_index);
typedef uint8_t (*ui_template_virtual_slot_text_fn)(uint8_t slot,
                                                    char *out_name,
                                                    uint32_t out_name_len,
                                                    char *out_value,
                                                    uint32_t out_value_len);
typedef uint8_t (*ui_template_param_text_fn)(uint8_t slot,
                                             param_id_t id,
                                             float value,
                                             char *out_name,
                                             uint32_t out_name_len,
                                             char *out_value,
                                             uint32_t out_value_len);

typedef struct
{
    const ui_template_family_t *family;
    ui_template_family_resolver_fn family_resolver;
    ui_template_widget_picker_fn widget_picker;
    ui_template_subpage_enabled_fn subpage_enabled;
    ui_template_virtual_slot_text_fn virtual_slot_text;
    ui_template_param_text_fn param_text;
    const ui_template_family_t *resolved_family;
    uint8_t active_subpage;
    uint8_t has_visited;
} ui_template_page_state_t;

void ui_template_page_enter(void);
void ui_template_page_leave(void);
void ui_template_page_handle_event(const ui_event_t *ev);
void ui_template_page_tick(void);
void ui_template_page_sync_active_track_context(void);
void ui_template_page_render(void);

void ui_template_page_select_subpage(ui_template_page_state_t *state, uint8_t subpage_index);
uint8_t ui_template_page_is_subpage_selectable(const ui_template_page_state_t *state, uint8_t subpage_index);
void ui_template_page_normalize_active_subpage(ui_template_page_state_t *state);
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
