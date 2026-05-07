#include "ui_template_page.h"

#include <stddef.h>
#include <string.h>

#include "buttons.h"
#include "Storage/memory_layout.h"
#include "ui_page_manager.h"
#include "ui_renderer_template.h"

UI_SDRAM static const ui_template_family_t *g_ui_template_family_registry[UI_TEMPLATE_FAMILY_COUNT][UI_TRACK_FAMILY_COUNT][UI_TRACK_TYPE_COUNT];

static ui_template_page_state_t *ui_template_page_get_active_state(void)
{
    const ui_page_t *page = ui_page_get();
    if ((page == 0) || (page->context == 0))
    {
        return 0;
    }

    return (ui_template_page_state_t *)page->context;
}

static uint8_t ui_template_page_is_subpage_enabled(const ui_template_page_state_t *state, uint8_t subpage_index)
{
    if (subpage_index >= 4U)
    {
        return 0U;
    }

    if ((state != NULL) && (state->subpage_enabled != NULL))
    {
        return (state->subpage_enabled(subpage_index) != 0U) ? 1U : 0U;
    }

    return 1U;
}

static uint8_t ui_template_subpage_has_param(const ui_template_subpage_t *subpage)
{
    if (subpage == NULL)
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < 4U; ++i)
    {
        if (subpage->param_bank.params[i] < PARAM_COUNT)
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t ui_template_subpage_has_non_empty_title(const ui_template_subpage_t *subpage)
{
    if ((subpage == NULL) || (subpage->title == NULL) || (subpage->title[0] == '\0'))
    {
        return 0U;
    }

    if ((strcmp(subpage->title, "-") == 0) || (strcmp(subpage->title, "N/A") == 0))
    {
        return 0U;
    }

    return 1U;
}

uint8_t ui_template_page_is_subpage_selectable(const ui_template_page_state_t *state, uint8_t subpage_index)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);
    if ((state == NULL)
            || (family == NULL)
            || (subpage_index >= 4U)
            || (ui_template_page_is_subpage_enabled(state, subpage_index) == 0U))
    {
        return 0U;
    }

    const ui_template_subpage_t *const subpage = &family->subpages[subpage_index];
    if (ui_template_subpage_has_param(subpage) != 0U)
    {
        return 1U;
    }

    return ui_template_subpage_has_non_empty_title(subpage);
}

static uint8_t ui_template_page_get_first_selectable_subpage(const ui_template_page_state_t *state, uint8_t fallback)
{
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        if (ui_template_page_is_subpage_selectable(state, i) != 0U)
        {
            return i;
        }
    }

    return fallback % 4U;
}

void ui_template_page_normalize_active_subpage(ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);
    if ((state == NULL) || (family == NULL))
    {
        return;
    }

    if ((state->active_subpage >= 4U)
            || (ui_template_page_is_subpage_selectable(state, state->active_subpage) == 0U))
    {
        state->active_subpage = ui_template_page_get_first_selectable_subpage(state, family->default_subpage);
    }
}

static void ui_template_page_sync_resolved_family(ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);
    if ((state == 0) || (family == 0))
    {
        return;
    }

    if (state->resolved_family != family)
    {
        state->resolved_family = family;
        state->active_subpage = ui_template_page_get_first_selectable_subpage(state, family->default_subpage);
    }
}

const ui_template_family_t *ui_template_page_get_active_family(const ui_template_page_state_t *state)
{
    if (state == 0)
    {
        return 0;
    }

    if (state->family_resolver != 0)
    {
        return state->family_resolver();
    }

    return state->family;
}

void ui_template_family_registry_init(void)
{
    for (uint8_t family_index = 0U; family_index < (uint8_t)UI_TEMPLATE_FAMILY_COUNT; family_index++)
    {
        for (uint8_t track_family_index = 0U; track_family_index < (uint8_t)UI_TRACK_FAMILY_COUNT; track_family_index++)
        {
            for (uint8_t type_index = 0U; type_index < (uint8_t)UI_TRACK_TYPE_COUNT; type_index++)
            {
                g_ui_template_family_registry[family_index][track_family_index][type_index] = 0;
            }
        }
    }
}

void ui_template_family_register(ui_template_family_id_t family_id,
                                 ui_track_family_t track_family,
                                 ui_track_type_t track_type,
                                 const ui_template_family_t *family)
{
    if (((uint8_t)family_id >= (uint8_t)UI_TEMPLATE_FAMILY_COUNT)
            || ((uint8_t)track_family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
            || ((uint8_t)track_type >= (uint8_t)UI_TRACK_TYPE_COUNT)
            || !ui_track_type_is_valid_for_family(track_family, track_type))
    {
        return;
    }

    g_ui_template_family_registry[family_id][track_family][track_type] = family;
}

const ui_template_family_t *ui_template_family_resolve(ui_template_family_id_t family_id,
                                                       uint8_t track,
                                                       ui_track_family_t track_family,
                                                       ui_track_type_t track_type)
{
    (void)track;

    if (((uint8_t)family_id >= (uint8_t)UI_TEMPLATE_FAMILY_COUNT)
            || ((uint8_t)track_family >= (uint8_t)UI_TRACK_FAMILY_COUNT)
            || ((uint8_t)track_type >= (uint8_t)UI_TRACK_TYPE_COUNT)
            || !ui_track_type_is_valid_for_family(track_family, track_type))
    {
        return 0;
    }

    return g_ui_template_family_registry[family_id][track_family][track_type];
}

const ui_template_family_t *ui_template_family_resolve_active_track(ui_template_family_id_t family_id)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_track_config_t config = ui_get_track_config(active_track);
    return ui_template_family_resolve(family_id, active_track, config.family, config.type);
}

static void ui_template_page_apply_active_bank(ui_template_page_state_t *state)
{
    ui_template_page_sync_resolved_family(state);
    ui_template_page_normalize_active_subpage(state);

    const ui_template_subpage_t *subpage = ui_template_page_get_active_subpage(state);
    if (subpage == 0)
    {
        ui_param_set_bank(0);
        return;
    }

    ui_param_set_bank(&subpage->param_bank);
}

void ui_template_page_select_subpage(ui_template_page_state_t *state, uint8_t subpage_index)
{
    if ((state == 0)
            || (ui_template_page_get_active_family(state) == 0)
            || (subpage_index >= 4U)
            || (ui_template_page_is_subpage_selectable(state, subpage_index) == 0U))
    {
        if ((state != 0) && (subpage_index == state->active_subpage))
        {
            ui_template_page_apply_active_bank(state);
        }
        return;
    }

    state->active_subpage = subpage_index;
    state->has_visited = 1U;
    ui_template_page_apply_active_bank(state);
}

const ui_template_subpage_t *ui_template_page_get_active_subpage(const ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);
    if ((family == 0) || (state == 0) || (state->active_subpage >= 4U))
    {
        return 0;
    }

    return &family->subpages[state->active_subpage];
}

void ui_template_page_enter(void)
{
    ui_template_page_state_t *state = ui_template_page_get_active_state();
    const ui_template_family_t *family = ui_template_page_get_active_family(state);
    if (family == 0)
    {
        ui_param_set_bank(0);
        return;
    }

    ui_template_page_sync_resolved_family(state);

    if ((state->has_visited == 0U) || (state->active_subpage >= 4U))
    {
        state->active_subpage = ui_template_page_get_first_selectable_subpage(state, family->default_subpage);
    }
    else if (ui_template_page_is_subpage_selectable(state, state->active_subpage) == 0U)
    {
        state->active_subpage = ui_template_page_get_first_selectable_subpage(state, family->default_subpage);
    }

    state->has_visited = 1U;
    ui_template_page_apply_active_bank(state);
}

void ui_template_page_leave(void)
{
}

void ui_template_page_handle_event(const ui_event_t *ev)
{
    if ((ev == 0) || (ev->type != UI_EVENT_BUTTON_PRESS))
    {
        return;
    }

    ui_template_page_state_t *state = ui_template_page_get_active_state();
    if ((state == 0) || (ui_template_page_get_active_family(state) == 0))
    {
        return;
    }

    switch ((button_id_t)ev->id)
    {
        case BTN_PAGE_1:
            ui_template_page_select_subpage(state, 0U);
            break;

        case BTN_PAGE_2:
            ui_template_page_select_subpage(state, 1U);
            break;

        case BTN_PAGE_3:
            ui_template_page_select_subpage(state, 2U);
            break;

        case BTN_PAGE_4:
            ui_template_page_select_subpage(state, 3U);
            break;

        default:
            break;
    }
}

void ui_template_page_tick(void)
{
    ui_template_page_sync_active_track_context();
}

void ui_template_page_sync_active_track_context(void)
{
    ui_template_page_state_t *state = ui_template_page_get_active_state();
    if (state == 0)
    {
        ui_param_set_bank(0);
        return;
    }

    ui_template_page_apply_active_bank(state);
}

void ui_template_page_render(void)
{
    ui_renderer_template_draw(ui_template_page_get_active_state());
}
