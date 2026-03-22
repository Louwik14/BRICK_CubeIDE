#include "ui_template_page.h"

#include "buttons.h"
#include "ui_page_manager.h"
#include "ui_renderer_template.h"

static ui_template_page_state_t *ui_template_page_get_active_state(void)
{
    const ui_page_t *page = ui_page_get();
    if ((page == 0) || (page->context == 0))
    {
        return 0;
    }

    return (ui_template_page_state_t *)page->context;
}

static void ui_template_page_apply_active_bank(ui_template_page_state_t *state)
{
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
    if ((state == 0) || (state->family == 0) || (subpage_index >= 4U))
    {
        return;
    }

    state->active_subpage = subpage_index;
    state->has_visited = 1U;
    ui_template_page_apply_active_bank(state);
}

const ui_template_subpage_t *ui_template_page_get_active_subpage(const ui_template_page_state_t *state)
{
    if ((state == 0) || (state->family == 0) || (state->active_subpage >= 4U))
    {
        return 0;
    }

    return &state->family->subpages[state->active_subpage];
}

void ui_template_page_enter(void)
{
    ui_template_page_state_t *state = ui_template_page_get_active_state();
    if ((state == 0) || (state->family == 0))
    {
        ui_param_set_bank(0);
        return;
    }

    if ((state->has_visited == 0U) || (state->active_subpage >= 4U))
    {
        state->active_subpage = state->family->default_subpage % 4U;
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
    if ((state == 0) || (state->family == 0))
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
}

void ui_template_page_render(void)
{
    ui_renderer_template_draw(ui_template_page_get_active_state());
}
