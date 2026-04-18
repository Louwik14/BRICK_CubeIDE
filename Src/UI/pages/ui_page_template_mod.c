#include "pages/ui_page_template_mod.h"

#include "ui_core.h"
#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_mod_family = {
    .family_title = "MOD",
    .nav_labels = { "LFO1", "LFO2", "-", "-" },
    .subpages = {
        {
            .title = "LFO1",
            .param_bank = { .params = { PARAM_LFO1_DEST, PARAM_LFO1_RATE, PARAM_LFO1_DEPTH, PARAM_LFO1_SHAPE } },
        },
        {
            .title = "LFO2",
            .param_bank = { .params = { PARAM_LFO2_DEST, PARAM_LFO2_RATE, PARAM_LFO2_DEPTH, PARAM_LFO2_SHAPE } },
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

static const ui_template_family_t *ui_page_template_mod_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_MOD);
}

static ui_template_page_state_t g_ui_template_mod_state = {
    .family = 0,
    .family_resolver = ui_page_template_mod_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_mod_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; ++family)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        for (uint8_t type = 0U; type < (uint8_t)UI_TRACK_TYPE_COUNT; ++type)
        {
            const ui_track_type_t track_type = (ui_track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type))
            {
                continue;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_MOD,
                                        track_family,
                                        track_type,
                                        &g_ui_template_mod_family);
        }
    }
}

const ui_page_t g_ui_page_template_mod = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_mod_state,
};
