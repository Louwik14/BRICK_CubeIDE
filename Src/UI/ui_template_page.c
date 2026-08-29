#include "ui_template_page.h"
#include "Track/entity_topology.h"

#include <stddef.h>
#include <string.h>

#include "buttons.h"
#include "Track/track_runtime.h"
#include "Platform/memory_layout.h"
#include "pages/ui_page_template_tone.h"
#include "ui_page_manager.h"
#include "ui_navigation.h"
#include "ui_renderer_template.h"

UI_STATE_SDRAM static const ui_template_family_t *g_ui_template_family_registry[UI_TEMPLATE_FAMILY_COUNT][UI_TRACK_FAMILY_COUNT][UI_TRACK_TYPE_COUNT];

static ui_template_page_state_t *ui_template_page_get_active_state(void)
{
    const ui_page_t *page = ui_page_get();
    if ((page == 0) || (page->context == 0))
    {
        return 0;
    }

    return (ui_template_page_state_t *)page->context;
}

static uint8_t ui_template_page_state_is_active(const ui_template_page_state_t *state)
{
    return (uint8_t)((state != NULL) && (state == ui_template_page_get_active_state()));
}

static void ui_template_page_remember_if_active(const ui_template_page_state_t *state)
{
    if (ui_template_page_state_is_active(state) == 0U)
    {
        return;
    }

    ui_navigation_remember_template_subpage(ui_page_get_id(),
                                             state->resolved_navigation_subset,
                                             state->active_subpage);
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
        ui_template_page_remember_if_active(state);
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
        const uint8_t previous_subset = state->resolved_navigation_subset;
        const uint8_t previous_subpage = state->active_subpage;
        if ((state->resolved_family != 0)
                && (ui_template_page_state_is_active(state) != 0U)
                && (previous_subpage < 4U))
        {
            ui_navigation_remember_template_subpage(ui_page_get_id(),
                                                     previous_subset,
                                                     previous_subpage);
        }
        state->resolved_family = family;
        state->resolved_navigation_subset = state->navigation_subset;
        state->active_subpage = ((state->preserve_subpage_on_family_change != 0U)
                && (previous_subpage < 4U)
                && (ui_template_page_is_subpage_selectable(state, previous_subpage) != 0U))
                ? previous_subpage
                : ui_template_page_get_first_selectable_subpage(state, family->default_subpage);
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
    const uint8_t active_track = ui_get_active_lane();
    const ui_track_config_t config = ui_get_track_config(active_track);
    return ui_template_family_resolve(family_id, active_track, config.family, config.type);
}

static uint8_t ui_template_family_from_page(uint8_t page_id,
                                             ui_template_family_id_t *out_family_id)
{
    if (out_family_id == 0)
    {
        return 0U;
    }

    switch (page_id)
    {
        case UI_PAGE_TEMPLATE_ENV: *out_family_id = UI_TEMPLATE_FAMILY_ENV; return 1U;
        case UI_PAGE_TEMPLATE_CFG:
        case UI_PAGE_TEMPLATE_REC_CFG: *out_family_id = UI_TEMPLATE_FAMILY_CFG; return 1U;
        case UI_PAGE_TEMPLATE_TONE: *out_family_id = UI_TEMPLATE_FAMILY_TONE; return 1U;
        case UI_PAGE_TEMPLATE_MOD: *out_family_id = UI_TEMPLATE_FAMILY_MOD; return 1U;
        case UI_PAGE_TEMPLATE_KEYBOARD: *out_family_id = UI_TEMPLATE_FAMILY_KEYBOARD; return 1U;
        case UI_PAGE_MIDI_FX:
        case UI_PAGE_AUDIO_FX: *out_family_id = UI_TEMPLATE_FAMILY_FX; return 1U;
        case UI_PAGE_TEMPLATE_SEQ: *out_family_id = UI_TEMPLATE_FAMILY_SEQ; return 1U;
        case UI_PAGE_TEMPLATE_MIX: *out_family_id = UI_TEMPLATE_FAMILY_MIX; return 1U;
        case UI_PAGE_TEMPLATE_PLAY: *out_family_id = UI_TEMPLATE_FAMILY_PLAY; return 1U;
        default: return 0U;
    }
}

uint8_t ui_template_edit_context_resolve(ui_template_family_id_t family_id,
                                         uint8_t selected_entity,
                                         ui_template_edit_context_t *out_context)
{
    entity_topology_descriptor_t topology;
    if ((out_context == 0)
            || ((uint8_t)family_id >= (uint8_t)UI_TEMPLATE_FAMILY_COUNT)
            || (entity_topology_get(selected_entity, &topology) == 0U)
            || (topology.active == 0U))
    {
        return 0U;
    }

    out_context->selected_entity = topology.entity_id;
    out_context->owner_entity = (uint8_t)(((family_id == UI_TEMPLATE_FAMILY_MOD)
            && (topology.role == ENTITY_ROLE_GROUP_CHILD))
            ? topology.parent_entity_id : topology.entity_id);
    out_context->role = topology.role;
    out_context->family_id = family_id;
    return 1U;
}

uint8_t ui_template_edit_context_resolve_active(ui_template_edit_context_t *out_context)
{
    ui_template_family_id_t family_id = UI_TEMPLATE_FAMILY_COUNT;
    return (uint8_t)((ui_template_family_from_page(ui_page_get_id(), &family_id) != 0U)
            && (ui_template_edit_context_resolve(family_id,
                                                 ui_get_active_lane(),
                                                 out_context) != 0U));
}

uint8_t ui_template_family_resolve_owner_track(ui_template_family_id_t family_id,
                                                uint8_t selected_track,
                                                uint8_t *out_owner_track)
{
    ui_template_edit_context_t context;
    if ((out_owner_track == 0)
            || (ui_template_edit_context_resolve(family_id,
                                                 selected_track,
                                                 &context) == 0U))
    {
        return 0U;
    }

    *out_owner_track = context.owner_entity;
    return 1U;
}

static uint8_t ui_template_family_to_runtime_ensemble(ui_template_family_id_t family_id,
                                                       track_runtime_ui_ensemble_t *out_ensemble)
{
    if (out_ensemble == 0)
    {
        return 0U;
    }

    switch (family_id)
    {
        case UI_TEMPLATE_FAMILY_ENV: *out_ensemble = TRACK_RUNTIME_UI_ENSEMBLE_ENV; return 1U;
        case UI_TEMPLATE_FAMILY_CFG: *out_ensemble = TRACK_RUNTIME_UI_ENSEMBLE_CFG; return 1U;
        case UI_TEMPLATE_FAMILY_TONE: *out_ensemble = TRACK_RUNTIME_UI_ENSEMBLE_TONE; return 1U;
        case UI_TEMPLATE_FAMILY_MOD: *out_ensemble = TRACK_RUNTIME_UI_ENSEMBLE_MOD; return 1U;
        case UI_TEMPLATE_FAMILY_KEYBOARD: *out_ensemble = TRACK_RUNTIME_UI_ENSEMBLE_KEYBOARD; return 1U;
        case UI_TEMPLATE_FAMILY_FX: *out_ensemble = TRACK_RUNTIME_UI_ENSEMBLE_FX; return 1U;
        case UI_TEMPLATE_FAMILY_SEQ: *out_ensemble = TRACK_RUNTIME_UI_ENSEMBLE_SEQ; return 1U;
        case UI_TEMPLATE_FAMILY_MIX: *out_ensemble = TRACK_RUNTIME_UI_ENSEMBLE_MIX; return 1U;
        case UI_TEMPLATE_FAMILY_PLAY: *out_ensemble = TRACK_RUNTIME_UI_ENSEMBLE_PLAY; return 1U;
        default: return 0U;
    }
}

const ui_template_family_t *ui_template_family_resolve_effective_for_track(ui_template_family_id_t family_id,
                                                                            uint8_t track,
                                                                            uint8_t scope_index)
{
    uint8_t owner_track = 0U;
    if (ui_template_family_resolve_owner_track(family_id, track, &owner_track) == 0U)
    {
        return 0;
    }

    track_runtime_ui_ensemble_t ensemble = TRACK_RUNTIME_UI_ENSEMBLE_COUNT;
    if ((ui_template_family_to_runtime_ensemble(family_id, &ensemble) == 0U)
            || (track_runtime_is_ui_ensemble_available(owner_track, ensemble) == 0U))
    {
        return 0;
    }

    if (family_id == UI_TEMPLATE_FAMILY_TONE)
    {
        return ui_page_template_tone_resolve_for_track(owner_track, scope_index);
    }

    const ui_track_config_t config = ui_get_track_config(owner_track);
    return ui_template_family_resolve(family_id, owner_track, config.family, config.type);
}

const ui_template_family_t *ui_template_family_resolve_effective_active_track(ui_template_family_id_t family_id)
{
    return ui_template_family_resolve_effective_for_track(family_id,
                                                           ui_get_active_lane(),
                                                           UI_TEMPLATE_EFFECTIVE_SCOPE_CURRENT);
}

uint8_t ui_template_family_get_effective_scope_count(ui_template_family_id_t family_id, uint8_t track)
{
    if (ui_template_family_resolve_effective_for_track(family_id,
                                                       track,
                                                       UI_TEMPLATE_EFFECTIVE_SCOPE_CURRENT) == 0)
    {
        return 0U;
    }

    return (uint8_t)(((family_id == UI_TEMPLATE_FAMILY_TONE)
            && (ui_page_template_tone_is_global_master() != 0U)) ? 3U : 1U);
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
    ui_template_page_sync_resolved_family(state);

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
    ui_template_page_remember_if_active(state);
    ui_template_page_apply_active_bank(state);
}

void ui_template_page_select_nearest_subpage(ui_template_page_state_t *state, uint8_t subpage_index)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);
    if ((state == 0) || (family == 0))
    {
        return;
    }

    uint8_t requested = subpage_index;
    if (requested >= 4U)
    {
        requested = 3U;
    }

    for (uint8_t i = 0U; i <= requested; ++i)
    {
        const uint8_t candidate = (uint8_t)(requested - i);
        if (ui_template_page_is_subpage_selectable(state, candidate) != 0U)
        {
            ui_template_page_select_subpage(state, candidate);
            return;
        }
    }

    ui_template_page_select_subpage(state,
                                    ui_template_page_get_first_selectable_subpage(state, family->default_subpage));
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
