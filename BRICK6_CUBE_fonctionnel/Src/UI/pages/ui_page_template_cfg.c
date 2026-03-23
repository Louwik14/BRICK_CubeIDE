#include "pages/ui_page_template_cfg.h"

#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_cfg_family = {
    .family_title = "CFG",
    .nav_labels = { "TRACK", "-", "-", "-" },
    .subpages = {
        {
            .title = "TRACK",
            .param_bank = { .params = { PARAM_CFG_TRACK, PARAM_CFG_TRACK_TYPE, PARAM_COUNT, PARAM_COUNT } },
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

static const ui_template_family_t *ui_page_template_cfg_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_CFG);
}

static ui_template_page_state_t g_ui_template_cfg_state = {
    .family = 0,
    .family_resolver = ui_page_template_cfg_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_cfg_register_families(void)
{
    ui_template_family_register(UI_TEMPLATE_FAMILY_CFG, UI_TRACK_TYPE_AUDIO, &g_ui_template_cfg_family);
    ui_template_family_register(UI_TEMPLATE_FAMILY_CFG, UI_TRACK_TYPE_SYNTH, &g_ui_template_cfg_family);
    ui_template_family_register(UI_TEMPLATE_FAMILY_CFG, UI_TRACK_TYPE_MIDI, &g_ui_template_cfg_family);
    ui_template_family_register(UI_TEMPLATE_FAMILY_CFG, UI_TRACK_TYPE_CARD, &g_ui_template_cfg_family);
}

const ui_page_t g_ui_page_template_cfg = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .render = ui_template_page_render,
    .context = &g_ui_template_cfg_state,
};
