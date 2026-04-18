#include "pages/ui_page_template_seq.h"

#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_seq_family = {
    .family_title = "SEQ",
    .nav_labels = { "SEQ", "-", "-", "-" },
    .subpages = {
        { .title = "SEQ", .param_bank = { .params = { PARAM_SEQ_LENGTH, PARAM_SEQ_DIV, PARAM_SEQ_QUANT, PARAM_SEQ_SWING } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

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
    for (uint8_t track_family = 0U; track_family < (uint8_t)UI_TRACK_FAMILY_COUNT; ++track_family)
    {
        for (uint8_t track_type = 0U; track_type < (uint8_t)UI_TRACK_TYPE_COUNT; ++track_type)
        {
            if (!ui_track_type_is_valid_for_family((ui_track_family_t)track_family, (ui_track_type_t)track_type))
            {
                continue;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_SEQ,
                                        (ui_track_family_t)track_family,
                                        (ui_track_type_t)track_type,
                                        &g_ui_template_seq_family);
        }
    }
}

const ui_page_t g_ui_page_template_seq = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_seq_state,
};
