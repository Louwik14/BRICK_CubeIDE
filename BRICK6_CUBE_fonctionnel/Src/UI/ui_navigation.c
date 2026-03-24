#include "ui_navigation.h"

#include "ui_page_manager.h"

/*
 * Data-driven navigation table.
 * To add a new workflow, add/edit rules here without changing the engine logic.
 */
static const ui_nav_rule_t g_ui_nav_rules[] = {
    { BTN_PARAM_1, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_FILTER },
    { BTN_PARAM_2, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_CFG },
    { BTN_PARAM_3, UI_NAV_ANY_PAGE, UI_PAGE_MAIN },

    /* calibration page */
    { BTN_PARAM_4, UI_NAV_ANY_PAGE, UI_PAGE_CALIBRATION },
    { BTN_PARAM_5, UI_NAV_ANY_PAGE, UI_PAGE_USER_CALIBRATION },
    { BTN_PARAM_6, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_DX7 },
    { BTN_PARAM_8, UI_NAV_ANY_PAGE, UI_PAGE_HALL_KEY_DEBUG },
};

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
                && ((rule->required_page == UI_NAV_ANY_PAGE) || (rule->required_page == current_page)))
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
            return rule->button;
        }
    }

    return BTN_COUNT;
}
