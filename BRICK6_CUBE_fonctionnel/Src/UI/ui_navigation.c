#include "ui_navigation.h"

#include "ui_core.h"
#include "ui_page_manager.h"

/*
 * Data-driven navigation table.
 * To add a new workflow, add/edit rules here without changing the engine logic.
 */
static const ui_nav_rule_t g_ui_nav_rules[] = {
    { BTN_PARAM_1, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_COLORS },
    { BTN_PARAM_2, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_DX7 },
    { BTN_PARAM_4, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_MIX },
    { BTN_PARAM_5, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_PLAY },
};

static uint8_t ui_navigation_is_page_available(uint8_t page_id)
{
    if (page_id == UI_PAGE_TEMPLATE_PLAY)
    {
        return (ui_get_track_family(ui_get_active_track()) == UI_TRACK_FAMILY_SYNTH) ? 1U : 0U;
    }
    if (page_id == UI_PAGE_TEMPLATE_MIX)
    {
        const ui_track_family_t family = ui_get_track_family(ui_get_active_track());
        return (ui_track_family_is_input(family) || (family == UI_TRACK_FAMILY_SYNTH)) ? 1U : 0U;
    }

    return 1U;
}

void ui_navigation_handle_event(const ui_event_t *event)
{
    if ((event == 0) || (event->type != UI_EVENT_BUTTON_PRESS))
    {
        return;
    }

    const uint8_t current_page = ui_page_get_id();

    for (uint8_t i = 0U; i < (uint8_t)(sizeof(g_ui_nav_rules) / sizeof(g_ui_nav_rules[0])); i++)
    {
        const ui_nav_rule_t *rule = &g_ui_nav_rules[i];

        if ((event->id == (uint8_t)rule->button)
                && ((rule->required_page == UI_NAV_ANY_PAGE) || (rule->required_page == current_page))
                && (ui_navigation_is_page_available(rule->target_page) != 0U))
        {
            if (rule->target_page != current_page)
            {
                ui_page_set(rule->target_page);
            }
            break;
        }
    }
}

button_id_t ui_navigation_get_button_for_page(uint8_t page_id)
{
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(g_ui_nav_rules) / sizeof(g_ui_nav_rules[0])); i++)
    {
        const ui_nav_rule_t *rule = &g_ui_nav_rules[i];

        if (rule->target_page == page_id)
        {
            if (ui_navigation_is_page_available(page_id) != 0U)
            {
                return rule->button;
            }
            break;
        }
    }

    return BTN_COUNT;
}
