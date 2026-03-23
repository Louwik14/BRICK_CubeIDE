#include "pages/ui_page_template_filter.h"

#include "mixer.h"
#include "param_store.h"
#include "ui_template_page.h"

static ui_template_family_t g_ui_template_filter_family = {
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

static void ui_page_template_filter_sync_family(void)
{
    const uint8_t is_eq3 = ((uint8_t)(param_store_get_active(PARAM_FILTER_TYPE) + 0.5f) == (uint8_t)MIXER_TRACK_FILTER_EQ3) ? 1U : 0U;

    g_ui_template_filter_family.nav_labels[0] = "MAIN";
    g_ui_template_filter_family.nav_labels[1] = (is_eq3 != 0U) ? "-" : "ENV";
    g_ui_template_filter_family.nav_labels[2] = "-";
    g_ui_template_filter_family.nav_labels[3] = "-";

    g_ui_template_filter_family.subpages[0].title = "MAIN";
    g_ui_template_filter_family.subpages[0].param_bank.params[0] = PARAM_FILTER_TYPE;
    g_ui_template_filter_family.subpages[0].param_bank.params[1] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_LOW : PARAM_FILTER_CUTOFF;
    g_ui_template_filter_family.subpages[0].param_bank.params[2] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_MID : PARAM_FILTER_RESONANCE;
    g_ui_template_filter_family.subpages[0].param_bank.params[3] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_HIGH : PARAM_FILTER_EG_AMT;

    g_ui_template_filter_family.subpages[1].title = (is_eq3 != 0U) ? "-" : "ENV";
    g_ui_template_filter_family.subpages[1].param_bank.params[0] = (is_eq3 != 0U) ? PARAM_COUNT : PARAM_FILTER_ATTACK;
    g_ui_template_filter_family.subpages[1].param_bank.params[1] = (is_eq3 != 0U) ? PARAM_COUNT : PARAM_FILTER_DECAY;
    g_ui_template_filter_family.subpages[1].param_bank.params[2] = (is_eq3 != 0U) ? PARAM_COUNT : PARAM_FILTER_SUSTAIN;
    g_ui_template_filter_family.subpages[1].param_bank.params[3] = (is_eq3 != 0U) ? PARAM_COUNT : PARAM_FILTER_RELEASE;
}

static void ui_page_template_filter_enter(void)
{
    ui_page_template_filter_sync_family();
    ui_template_page_enter();
}

static void ui_page_template_filter_handle_event(const ui_event_t *ev)
{
    ui_page_template_filter_sync_family();
    ui_template_page_handle_event(ev);
    ui_page_template_filter_sync_family();
    ui_template_page_select_subpage(&g_ui_template_filter_state, g_ui_template_filter_state.active_subpage);
}

static void ui_page_template_filter_tick(void)
{
    ui_page_template_filter_sync_family();
    ui_template_page_select_subpage(&g_ui_template_filter_state, g_ui_template_filter_state.active_subpage);
    ui_template_page_tick();
}

static void ui_page_template_filter_render(void)
{
    ui_page_template_filter_sync_family();
    ui_template_page_render();
}

const ui_page_t g_ui_page_template_filter = {
    .enter = ui_page_template_filter_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_page_template_filter_handle_event,
    .tick = ui_page_template_filter_tick,
    .render = ui_page_template_filter_render,
    .context = &g_ui_template_filter_state,
};
