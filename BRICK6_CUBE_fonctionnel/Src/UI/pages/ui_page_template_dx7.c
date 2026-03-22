#include "pages/ui_page_template_dx7.h"

#include "param_store.h"
#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_dx7_family = {
    .family_title = "FM DX7",
    .nav_labels = { "PLAY", "MOT", "CTRL", "COL" },
    .subpages = {
        {
            .title = "PLAY",
            .param_bank = { .params = { PARAM_DX7_ALGORITHM, PARAM_DX7_FEEDBACK, PARAM_MIX_TRACK3_GAIN, PARAM_DX7_TRANSPOSE } },
        },
        {
            .title = "MOTION",
            .param_bank = { .params = { PARAM_DX7_LFO_SPEED, PARAM_DX7_LFO_DELAY, PARAM_DX7_LFO_PITCH_MOD_DEPTH, PARAM_DX7_LFO_AMP_MOD_DEPTH } },
        },
        {
            .title = "CTRL",
            .param_bank = { .params = { PARAM_DX7_PITCH_BEND_RANGE, PARAM_DX7_PORTAMENTO_TIME, PARAM_DX7_MONO_MODE, PARAM_DX7_OPERATOR_MASK } },
        },
        {
            .title = "COLOR",
            .param_bank = { .params = { PARAM_DX7_OPERATOR_1_LEVEL, PARAM_DX7_OPERATOR_2_LEVEL, PARAM_DX7_OPERATOR_3_LEVEL, PARAM_DX7_OPERATOR_4_LEVEL } },
        },
    },
    .default_subpage = 0U,
};

static ui_template_page_state_t g_ui_template_dx7_state = {
    .family = &g_ui_template_dx7_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

const ui_page_t g_ui_page_template_dx7 = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .render = ui_template_page_render,
    .context = &g_ui_template_dx7_state,
};
