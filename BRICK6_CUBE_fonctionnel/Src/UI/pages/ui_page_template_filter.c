#include "pages/ui_page_template_filter.h"

#include "param_store.h"
#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_filter_family = {
    .family_title = "FILTER",
    .nav_labels = { "MAIN", "ENV", "-", "-" },
    .subpages = {
        {
            .title = "MAIN",
            .param_bank = { .params = { PARAM_FILTER_TYPE, PARAM_FILTER_CUTOFF, PARAM_FILTER_RESONANCE, PARAM_FILTER_EG_AMT } },
        },
        {
            .title = "ENV",
            .param_bank = { .params = { PARAM_FILTER_ATTACK, PARAM_FILTER_DECAY, PARAM_FILTER_SUSTAIN, PARAM_FILTER_RELEASE } },
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

static ui_template_page_state_t g_ui_template_filter_state = {
    .family = &g_ui_template_filter_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

const ui_page_t g_ui_page_template_filter = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .render = ui_template_page_render,
    .context = &g_ui_template_filter_state,
};
