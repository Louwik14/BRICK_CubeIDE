#include "pages/ui_page_template_mix.h"

#include "Core/track_runtime.h"
#include "Param/param_registry.h"
#include "ui_core.h"
#include "ui_template_page.h"

static uint8_t g_ui_template_mix_subset = 0U;

static const ui_template_family_t g_ui_template_mix_family_main = {
    .family_title = "MIX 1/2",
    .nav_labels = { "MIX", "REVB", "REV2", "REV3" },
    .subpages = {
        {
            .title = "MIX",
            .param_bank = { .params = { PARAM_MIX_LEVEL, PARAM_MIX_PAN, PARAM_MIX_SEND1, PARAM_MIX_SEND2 } },
        },
        {
            .title = "REVB",
            .param_bank = { .params = { PARAM_MIX_REVERB_WET, PARAM_MIX_REVERB_SIZE, PARAM_MIX_REVERB_DECAY, PARAM_MIX_REVERB_PRED } },
        },
        {
            .title = "REV2",
            .param_bank = { .params = { PARAM_MIX_REVERB_TYPE, PARAM_MIX_REVERB_SURR, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "REV3",
            .param_bank = { .params = { PARAM_MIX_REVERB_HPF, PARAM_MIX_REVERB_LPF, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_mix_family_delay_classic = {
    .family_title = "MIX 2/2",
    .nav_labels = { "DLY1", "DLY2", "-", "-" },
    .subpages = {
        {
            .title = "DLY1",
            .param_bank = { .params = { PARAM_MIX_DELAY_TYPE, PARAM_MIX_DELAY_TIME, PARAM_MIX_DELAY_PINGPONG, PARAM_MIX_DELAY_VOL } },
        },
        {
            .title = "DLY2",
            .param_bank = { .params = { PARAM_MIX_DELAY_HPF, PARAM_MIX_DELAY_LPF, PARAM_MIX_DELAY_REV, PARAM_MIX_DELAY_FEEDBACK } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_mix_family_delay_dual = {
    .family_title = "MIX 2/2",
    .nav_labels = { "DLY1", "DLY2", "DLY3", "DLY4" },
    .subpages = {
        {
            .title = "DLY1",
            .param_bank = { .params = { PARAM_MIX_DELAY_TYPE, PARAM_MIX_DELAY_TIME, PARAM_MIX_DELAY_MODE, PARAM_MIX_DELAY_VOL } },
        },
        {
            .title = "DLY2",
            .param_bank = { .params = { PARAM_MIX_DELAY_HPF, PARAM_MIX_DELAY_LPF, PARAM_MIX_DELAY_REV, PARAM_MIX_DELAY_FEEDBACK } },
        },
        {
            .title = "DLY3",
            .param_bank = { .params = { PARAM_MIX_DELAY_TIME_R, PARAM_MIX_DELAY_WIDTH, PARAM_MIX_DELAY_FBW, PARAM_MIX_DELAY_MOD } },
        },
        {
            .title = "DLY4",
            .param_bank = { .params = { PARAM_MIX_DELAY_MOD_RATE, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_mix_unavailable_family = {
    .family_title = "MIX",
    .nav_labels = { "MIX", "-", "-", "-" },
    .subpages = {
        {
            .title = "N/A",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t *ui_page_template_mix_resolve_family(void)
{
    /* Consumer-edge refresh: routability is read only after an explicit refresh. */
    track_runtime_refresh_track(ui_get_active_track());
    track_runtime_resolved_track_t resolved;
    if ((track_runtime_resolve_track(ui_get_active_track(), &resolved) == 0U)
            || (resolved.has_mix_target == 0U))
    {
        return &g_ui_template_mix_unavailable_family;
    }

    if (g_ui_template_mix_subset == 0U)
    {
        return &g_ui_template_mix_family_main;
    }

    return (param_get(PARAM_MIX_DELAY_TYPE) >= 0.5f)
            ? &g_ui_template_mix_family_delay_dual
            : &g_ui_template_mix_family_delay_classic;
}

static uint8_t ui_page_template_mix_subpage_enabled(uint8_t subpage_index)
{
    if (g_ui_template_mix_subset == 0U)
    {
        return (subpage_index < 4U) ? 1U : 0U;
    }

    const uint8_t delay_is_dual = (param_get(PARAM_MIX_DELAY_TYPE) >= 0.5f) ? 1U : 0U;
    const uint8_t page_count = (delay_is_dual != 0U) ? 4U : 2U;
    return (subpage_index < page_count) ? 1U : 0U;
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
    g_ui_template_mix_subset = 0U;
    g_ui_template_mix_state.resolved_family = ui_page_template_mix_resolve_family();
    ui_template_page_select_subpage(&g_ui_template_mix_state, 0U);
}

void ui_page_template_mix_toggle_subset(void)
{
    const uint8_t previous_subpage = g_ui_template_mix_state.active_subpage;
    g_ui_template_mix_subset = (g_ui_template_mix_subset == 0U) ? 1U : 0U;
    g_ui_template_mix_state.resolved_family = ui_page_template_mix_resolve_family();

    if (ui_page_template_mix_subpage_enabled(previous_subpage) != 0U)
    {
        ui_template_page_select_subpage(&g_ui_template_mix_state, previous_subpage);
        return;
    }

    ui_template_page_select_subpage(&g_ui_template_mix_state, 0U);
}

void ui_page_template_mix_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; ++family)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        if (!ui_track_family_is_input(track_family)
                && (ui_track_family_is_engine(track_family) == 0)
                && (track_family != UI_TRACK_FAMILY_MASTER))
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
                                        &g_ui_template_mix_family_main);
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
