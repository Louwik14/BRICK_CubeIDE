#include "pages/ui_page_template_mix.h"

#include "Core/track_runtime.h"
#include "ui_core.h"
#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_mix_family = {
    .family_title = "MIX",
    .nav_labels = { "MIX", "-", "-", "-" },
    .subpages = {
        {
            .title = "MIX",
            .param_bank = { .params = {
                PARAM_MIX_LEVEL, PARAM_MIX_PAN, PARAM_MIX_SEND1, PARAM_MIX_SEND2 } },
        },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_mix_unavailable_family = {
    .family_title = "MIX",
    .nav_labels = { "MIX", "-", "-", "-" },
    .subpages = {
        { .title = "N/A", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t *ui_page_template_mix_resolve_family(void)
{
    const uint8_t track = ui_get_active_lane();
    track_runtime_refresh_track(track);
    track_runtime_resolved_track_t resolved;
    if ((track_runtime_resolve_track(track, &resolved) == 0U)
            || (resolved.has_mix_target == 0U))
    {
        return &g_ui_template_mix_unavailable_family;
    }
    return &g_ui_template_mix_family;
}

static uint8_t ui_page_template_mix_subpage_enabled(uint8_t subpage_index)
{
    return (subpage_index == 0U) ? 1U : 0U;
}

static ui_template_page_state_t g_ui_template_mix_state = {
    .family = 0,
    .family_resolver = ui_page_template_mix_resolve_family,
    .subpage_enabled = ui_page_template_mix_subpage_enabled,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_mix_open_primary(void)
{
    g_ui_template_mix_state.resolved_family = ui_page_template_mix_resolve_family();
    ui_template_page_select_subpage(&g_ui_template_mix_state, 0U);
}

void ui_page_template_mix_toggle_subset(void)
{
    ui_page_template_mix_open_primary();
}

void ui_page_template_mix_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; ++family)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        if ((ui_track_family_is_engine(track_family) == 0)
                && (track_family != UI_TRACK_FAMILY_EXTERNAL))
        {
            continue;
        }

        for (uint8_t type = 0U; type < (uint8_t)UI_TRACK_TYPE_COUNT; ++type)
        {
            const ui_track_type_t track_type = (ui_track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type))
            {
                continue;
            }
            ui_template_family_register(UI_TEMPLATE_FAMILY_MIX,
                                        track_family,
                                        track_type,
                                        &g_ui_template_mix_family);
        }
    }
}

const ui_page_t g_ui_page_template_mix = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_mix_state,
};
