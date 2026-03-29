#include "pages/ui_page_template_play.h"

#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_play_family = {
    .family_title = "PLAY",
    .nav_labels = { "V1", "V2", "V3", "V4" },
    .subpages = {
        { .title = "Voice 1", .param_bank = { .params = { PARAM_SEQ_PLAY_V1_NOTE, PARAM_SEQ_PLAY_V1_VEL, PARAM_SEQ_PLAY_V1_LEN, PARAM_SEQ_PLAY_V1_MICTIM } } },
        { .title = "Voice 2", .param_bank = { .params = { PARAM_SEQ_PLAY_V2_NOTE, PARAM_SEQ_PLAY_V2_VEL, PARAM_SEQ_PLAY_V2_LEN, PARAM_SEQ_PLAY_V2_MICTIM } } },
        { .title = "Voice 3", .param_bank = { .params = { PARAM_SEQ_PLAY_V3_NOTE, PARAM_SEQ_PLAY_V3_VEL, PARAM_SEQ_PLAY_V3_LEN, PARAM_SEQ_PLAY_V3_MICTIM } } },
        { .title = "Voice 4", .param_bank = { .params = { PARAM_SEQ_PLAY_V4_NOTE, PARAM_SEQ_PLAY_V4_VEL, PARAM_SEQ_PLAY_V4_LEN, PARAM_SEQ_PLAY_V4_MICTIM } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t *ui_page_template_play_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_PLAY);
}

static ui_template_page_state_t g_ui_template_play_state = {
    .family = 0,
    .family_resolver = ui_page_template_play_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_play_register_families(void)
{
    ui_template_family_register(UI_TEMPLATE_FAMILY_PLAY,
                                UI_TRACK_FAMILY_SYNTH,
                                UI_TRACK_TYPE_DX7,
                                &g_ui_template_play_family);
    ui_template_family_register(UI_TEMPLATE_FAMILY_PLAY,
                                UI_TRACK_FAMILY_SYNTH,
                                UI_TRACK_TYPE_MONOB,
                                &g_ui_template_play_family);
    ui_template_family_register(UI_TEMPLATE_FAMILY_PLAY,
                                UI_TRACK_FAMILY_SYNTH,
                                UI_TRACK_TYPE_TB3,
                                &g_ui_template_play_family);
}

const ui_page_t g_ui_page_template_play = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .render = ui_template_page_render,
    .context = &g_ui_template_play_state,
};
