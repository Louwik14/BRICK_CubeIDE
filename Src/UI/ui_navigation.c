#include "ui_navigation.h"

#include "ui_core.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"

/*
 * Data-driven navigation table.
 * To add a new workflow, add/edit rules here without changing the engine logic.
 */
static const ui_nav_rule_t g_ui_nav_rules[] = {
    { BTN_PARAM_1, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_COLORS },
    { BTN_PARAM_2, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_TONE },
    { BTN_PARAM_3, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_MOD },
    { BTN_PARAM_4, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_MIX },
    { BTN_PARAM_5, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_PLAY },
    { BTN_PARAM_6, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_VCA },
};

static uint8_t g_ui_requested_ensemble_page = UI_PAGE_TEMPLATE_CFG;

static uint8_t ui_navigation_is_ensemble_page(uint8_t page_id)
{
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(g_ui_nav_rules) / sizeof(g_ui_nav_rules[0])); i++)
    {
        if (g_ui_nav_rules[i].target_page == page_id)
        {
            return 1U;
        }
    }

    return 0U;
}

static uint8_t ui_navigation_is_page_available(uint8_t page_id)
{
    switch (page_id)
    {
        case UI_PAGE_TEMPLATE_COLORS:
            return (ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_COLORS) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_CFG:
        case UI_PAGE_TEMPLATE_REC_CFG:
            return 1U;

        case UI_PAGE_TEMPLATE_TONE:
            return (ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_TONE) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_MOD:
            return (ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_MOD) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_KEYBOARD:
            return (ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_KEYBOARD) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_ARP:
            return (ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_ARP) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_SEQ:
            return (ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_SEQ) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_MIX:
            return (ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_MIX) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_PLAY:
            return (ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_PLAY) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_VCA:
            return (ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_VCA) != 0) ? 1U : 0U;

        default:
            return 1U;
    }
}

static uint8_t ui_navigation_is_track_bound_template_page(uint8_t page_id)
{
    switch (page_id)
    {
        case UI_PAGE_TEMPLATE_COLORS:
        case UI_PAGE_TEMPLATE_CFG:
        case UI_PAGE_TEMPLATE_TONE:
        case UI_PAGE_TEMPLATE_MOD:
        case UI_PAGE_TEMPLATE_KEYBOARD:
        case UI_PAGE_TEMPLATE_ARP:
        case UI_PAGE_TEMPLATE_SEQ:
        case UI_PAGE_TEMPLATE_MIX:
        case UI_PAGE_TEMPLATE_PLAY:
        case UI_PAGE_TEMPLATE_VCA:
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t ui_navigation_resolve_effective_ensemble_page(void)
{
    if (ui_navigation_is_ensemble_page(g_ui_requested_ensemble_page) == 0U)
    {
        g_ui_requested_ensemble_page = UI_PAGE_TEMPLATE_CFG;
    }

    if (ui_navigation_is_page_available(g_ui_requested_ensemble_page) != 0U)
    {
        return g_ui_requested_ensemble_page;
    }

    return UI_PAGE_TEMPLATE_CFG;
}

void ui_navigation_request_ensemble_page(uint8_t page_id)
{
    if (page_id == UI_PAGE_TEMPLATE_CFG)
    {
        g_ui_requested_ensemble_page = page_id;
        if (ui_page_get_id() != UI_PAGE_TEMPLATE_CFG)
        {
            ui_page_set(UI_PAGE_TEMPLATE_CFG);
        }
        return;
    }

    if (ui_navigation_is_ensemble_page(page_id) == 0U)
    {
        return;
    }

    if (ui_navigation_is_page_available(page_id) == 0U)
    {
        return;
    }

    g_ui_requested_ensemble_page = page_id;
    if (ui_page_get_id() != page_id)
    {
        ui_page_set(page_id);
    }
}

void ui_navigation_request_page_with_availability(uint8_t page_id)
{
    if (page_id == UI_PAGE_TEMPLATE_CFG)
    {
        ui_navigation_request_ensemble_page(page_id);
        return;
    }

    if (ui_navigation_is_ensemble_page(page_id) != 0U)
    {
        ui_navigation_request_ensemble_page(page_id);
        return;
    }

    if ((ui_navigation_is_track_bound_template_page(page_id) != 0U)
            && (ui_navigation_is_page_available(page_id) == 0U))
    {
        ui_navigation_request_ensemble_page(UI_PAGE_TEMPLATE_CFG);
        return;
    }

    ui_page_set(page_id);
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
                && ((rule->required_page == UI_NAV_ANY_PAGE) || (rule->required_page == current_page)))
        {
            ui_navigation_request_ensemble_page(rule->target_page);
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

void ui_navigation_sync_active_track_ensemble(void)
{
    const uint8_t current_page = ui_page_get_id();
    if ((current_page == UI_PAGE_TEMPLATE_CFG) || (ui_navigation_is_ensemble_page(current_page) != 0U))
    {
        const uint8_t effective_page = ui_navigation_resolve_effective_ensemble_page();
        if (effective_page != current_page)
        {
            ui_page_set(effective_page);
        }
        return;
    }

    if ((ui_navigation_is_track_bound_template_page(current_page) != 0U)
            && (ui_navigation_is_page_available(current_page) == 0U))
    {
        ui_page_set(UI_PAGE_TEMPLATE_CFG);
        return;
    }

    if (ui_navigation_is_track_bound_template_page(current_page) == 0U)
    {
        return;
    }
}
