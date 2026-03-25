#include "pages/ui_page_template_seq.h"

#include "ui_template_page.h"

static const ui_template_family_t *ui_page_template_seq_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_SEQ);
}

static ui_template_page_state_t g_ui_template_seq_state = {
    .family = 0,
    .family_resolver = ui_page_template_seq_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_seq_register_families(void)
{
    /* Step 0 scaffold: no concrete SEQ parameter family registration yet. */
}

const ui_page_t g_ui_page_template_seq = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .render = ui_template_page_render,
    .context = &g_ui_template_seq_state,
};
