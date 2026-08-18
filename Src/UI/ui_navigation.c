#include "ui_navigation.h"

#include "ui_core.h"
#include "Core/track_runtime.h"
#include "pages/ui_page_template_mix.h"
#include "pages/ui_page_template_env.h"
#include "pages/ui_page_template_play.h"
#include "pages/ui_page_template_tone.h"
#include "pages/ui_page_template_mod.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"

/*
 * Data-driven navigation table.
 * To add a new workflow, add/edit rules here without changing the engine logic.
 */
static const ui_nav_rule_t g_ui_nav_rules[] = {
    { BTN_PARAM_1, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_ENV },
    { BTN_PARAM_2, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_TONE },
    { BTN_PARAM_3, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_MOD },
    { BTN_PARAM_4, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_MIX },
    { BTN_PARAM_5, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_PLAY },
    { BTN_PARAM_6, UI_NAV_ANY_PAGE, UI_PAGE_TEMPLATE_ENV },
};

static uint8_t g_ui_requested_ensemble_page = UI_PAGE_TEMPLATE_CFG;

/*
 * Keep the local page independently for each (ensemble, subset) pair instead
 * of sharing one value per ensemble page.
 */
#define UI_NAVIGATION_MAX_TEMPLATE_SUBSETS 4U
static uint8_t g_ui_last_subpage_by_subset[UI_PAGE_COUNT][UI_NAVIGATION_MAX_TEMPLATE_SUBSETS];

static void ui_navigation_refresh_active_track_runtime(void)
{
    /* Consumer-edge refresh: navigation reads projection only after an explicit refresh. */
    track_runtime_refresh_track(ui_get_active_lane());
}

static uint8_t ui_navigation_is_ensemble_page(uint8_t page_id)
{
    switch (page_id)
    {
        case UI_PAGE_TEMPLATE_ENV:
        case UI_PAGE_TEMPLATE_CFG:
        case UI_PAGE_TEMPLATE_TONE:
        case UI_PAGE_TEMPLATE_MOD:
        case UI_PAGE_TEMPLATE_KEYBOARD:
        case UI_PAGE_MIDI_FX:
        case UI_PAGE_TEMPLATE_SEQ:
        case UI_PAGE_TEMPLATE_MIX:
        case UI_PAGE_TEMPLATE_PLAY:
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t ui_navigation_is_page_available(uint8_t page_id)
{
    const uint8_t active_track = ui_get_active_lane();

    switch (page_id)
    {
        case UI_PAGE_TEMPLATE_ENV:
            return (ui_template_family_resolve_effective_active_track(UI_TEMPLATE_FAMILY_ENV) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_CFG:
        case UI_PAGE_TEMPLATE_REC_CFG:
            return 1U;

        case UI_PAGE_TEMPLATE_TONE:
            return (ui_template_family_resolve_effective_active_track(UI_TEMPLATE_FAMILY_TONE) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_MOD:
            return (ui_template_family_resolve_effective_active_track(UI_TEMPLATE_FAMILY_MOD) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_KEYBOARD:
            return (ui_template_family_resolve_effective_active_track(UI_TEMPLATE_FAMILY_KEYBOARD) != 0) ? 1U : 0U;

        case UI_PAGE_MIDI_FX:
            return (uint8_t)((track_runtime_is_ui_ensemble_available(active_track, TRACK_RUNTIME_UI_ENSEMBLE_FX) != 0U)
                || (ui_hall_mode_resolve_rout_context(active_track, ui_get_hall_mode()) != UI_HALL_ROUT_CONTEXT_NONE));

        case UI_PAGE_TEMPLATE_SEQ:
            return (ui_template_family_resolve_effective_active_track(UI_TEMPLATE_FAMILY_SEQ) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_MIX:
            return (ui_template_family_resolve_effective_active_track(UI_TEMPLATE_FAMILY_MIX) != 0) ? 1U : 0U;

        case UI_PAGE_TEMPLATE_PLAY:
            return (ui_template_family_resolve_effective_active_track(UI_TEMPLATE_FAMILY_PLAY) != 0) ? 1U : 0U;

        default:
            return 1U;
    }
}

static uint8_t ui_navigation_is_track_bound_template_page(uint8_t page_id)
{
    switch (page_id)
    {
        case UI_PAGE_TEMPLATE_ENV:
        case UI_PAGE_TEMPLATE_CFG:
        case UI_PAGE_TEMPLATE_TONE:
        case UI_PAGE_TEMPLATE_MOD:
        case UI_PAGE_TEMPLATE_KEYBOARD:
        case UI_PAGE_MIDI_FX:
        case UI_PAGE_TEMPLATE_SEQ:
        case UI_PAGE_TEMPLATE_MIX:
        case UI_PAGE_TEMPLATE_PLAY:
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
    ui_navigation_refresh_active_track_runtime();

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
    ui_navigation_refresh_active_track_runtime();

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

    ui_navigation_refresh_active_track_runtime();

    const uint8_t current_page = ui_page_get_id();
    if (current_page == UI_PAGE_SETTINGS)
    {
        return;
    }

    for (uint8_t i = 0U; i < (uint8_t)(sizeof(g_ui_nav_rules) / sizeof(g_ui_nav_rules[0])); i++)
    {
        const ui_nav_rule_t *rule = &g_ui_nav_rules[i];

        if ((event->id == (uint8_t)rule->button)
                && ((rule->required_page == UI_NAV_ANY_PAGE) || (rule->required_page == current_page)))
        {
            if ((rule->button == BTN_PARAM_6) && (rule->target_page == UI_PAGE_TEMPLATE_ENV))
            {
                ui_navigation_request_ensemble_page(UI_PAGE_TEMPLATE_ENV);
                (void)ui_page_template_env_open_vca();
                break;
            }

            if ((rule->target_page == UI_PAGE_TEMPLATE_MIX) && (current_page == UI_PAGE_TEMPLATE_MIX))
            {
                ui_page_template_mix_toggle_subset();
                break;
            }
            if ((rule->target_page == UI_PAGE_TEMPLATE_ENV) && (current_page == UI_PAGE_TEMPLATE_ENV))
            {
                ui_page_template_env_toggle_subset();
                break;
            }
            if ((rule->target_page == UI_PAGE_TEMPLATE_TONE) && (current_page == UI_PAGE_TEMPLATE_TONE))
            {
                ui_page_template_tone_toggle_subset();
                break;
            }
            if ((rule->target_page == UI_PAGE_TEMPLATE_PLAY) && (current_page == UI_PAGE_TEMPLATE_PLAY))
            {
                ui_page_template_play_toggle_subset();
                break;
            }
            if ((rule->target_page == UI_PAGE_TEMPLATE_MOD) && (current_page == UI_PAGE_TEMPLATE_MOD))
            {
                ui_page_template_mod_toggle_subset();
                break;
            }

            ui_navigation_request_ensemble_page(rule->target_page);
            break;
        }
    }
}

uint8_t ui_navigation_is_ensemble_button_available(button_id_t button)
{
    ui_navigation_refresh_active_track_runtime();

    for (uint8_t i = 0U; i < (uint8_t)(sizeof(g_ui_nav_rules) / sizeof(g_ui_nav_rules[0])); i++)
    {
        const ui_nav_rule_t *rule = &g_ui_nav_rules[i];

        if (rule->button == button)
        {
            if (button == BTN_PARAM_6)
            {
                return (uint8_t)((ui_navigation_is_page_available(UI_PAGE_TEMPLATE_ENV) != 0U)
                        && (track_runtime_supports_vca_gate(track_runtime_get_ctx(ui_get_active_lane())) != 0U));
            }
            return ui_navigation_is_page_available(rule->target_page);
        }
    }

    return 1U;
}

button_id_t ui_navigation_get_button_for_page(uint8_t page_id)
{
    ui_navigation_refresh_active_track_runtime();

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
    ui_navigation_refresh_active_track_runtime();

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

void ui_navigation_sync_created_track_destination(void)
{
    g_ui_requested_ensemble_page = UI_PAGE_TEMPLATE_CFG;
    ui_navigation_refresh_active_track_runtime();

    if (ui_page_get_id() != UI_PAGE_TEMPLATE_CFG)
    {
        ui_page_set(UI_PAGE_TEMPLATE_CFG);
    }
}

void ui_navigation_remember_template_subpage(uint8_t page_id,
                                              uint8_t subset_index,
                                              uint8_t subpage_index)
{
    if ((ui_navigation_is_ensemble_page(page_id) == 0U)
            || (subset_index >= UI_NAVIGATION_MAX_TEMPLATE_SUBSETS)
            || (subpage_index >= 4U))
    {
        return;
    }

    g_ui_last_subpage_by_subset[page_id][subset_index] = subpage_index;
}

void ui_navigation_restore_current_template_subpage(void)
{
    const uint8_t page_id = ui_page_get_id();
    if (ui_navigation_is_ensemble_page(page_id) == 0U)
    {
        return;
    }

    const ui_page_t *page = ui_page_get();
    if ((page == 0) || (page->context == 0))
    {
        return;
    }

    const ui_template_page_state_t *state = (const ui_template_page_state_t *)page->context;
    const uint8_t subset_index = state->navigation_subset;
    const uint8_t last_subpage = (subset_index < UI_NAVIGATION_MAX_TEMPLATE_SUBSETS)
            ? g_ui_last_subpage_by_subset[page_id][subset_index]
            : 0U;

    g_ui_requested_ensemble_page = page_id;
    ui_template_page_select_nearest_subpage((ui_template_page_state_t *)page->context,
                                            last_subpage);
}
