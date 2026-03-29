#include "pages/ui_page_template_dx7.h"

#include "ui_core.h"
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

static const ui_template_family_t g_ui_template_tone_family_monob = {
    .family_title = "TONE",
    .nav_labels = { "OSC", "RNG", "DET", "MIX" },
    .subpages = {
        {
            .title = "OSC",
            .param_bank = { .params = { PARAM_MONOB_OSC1_WAVE, PARAM_MONOB_OSC2_WAVE, PARAM_MONOB_OSC3_WAVE, PARAM_MONOB_SUB_WAVE } },
        },
        {
            .title = "RANGE",
            .param_bank = { .params = { PARAM_MONOB_OSC1_RANGE, PARAM_MONOB_OSC2_RANGE, PARAM_MONOB_OSC3_RANGE, PARAM_MONOB_SUB_OCTAVE } },
        },
        {
            .title = "DETUNE",
            .param_bank = { .params = { PARAM_MONOB_OSC1_DETUNE, PARAM_MONOB_OSC2_DETUNE, PARAM_MONOB_OSC3_DETUNE, PARAM_COUNT } },
        },
        {
            .title = "MIX",
            .param_bank = { .params = { PARAM_MONOB_OSC1_MIX, PARAM_MONOB_OSC2_MIX, PARAM_MONOB_OSC3_MIX, PARAM_MONOB_SUB_MIX } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_tone_family_tb3 = {
    .family_title = "TONE",
    .nav_labels = { "MAIN", "-", "-", "-" },
    .subpages = {
        {
            .title = "MAIN",
            .param_bank = { .params = { PARAM_TB3_WAVEFORM, PARAM_TB3_VOLUME, PARAM_TB3_ACCENT, PARAM_TB3_SLIDE_TIME } },
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

static const ui_template_family_t *ui_page_template_dx7_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_TONE);
}

static ui_template_page_state_t g_ui_template_dx7_state = {
    .family = 0,
    .family_resolver = ui_page_template_dx7_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_dx7_register_families(void)
{
    for(uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; ++family)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        for(uint8_t type = 0U; type < (uint8_t)UI_TRACK_TYPE_COUNT; ++type)
        {
            const ui_track_type_t track_type = (ui_track_type_t)type;
            if(!ui_track_type_is_valid_for_family(track_family, track_type))
            {
                continue;
            }

            const ui_template_family_t *family_template = &g_ui_template_dx7_family;
            if((track_family == UI_TRACK_FAMILY_SYNTH) && (track_type == UI_TRACK_TYPE_MONOB))
            {
                family_template = &g_ui_template_tone_family_monob;
            }
            else if((track_family == UI_TRACK_FAMILY_SYNTH) && (track_type == UI_TRACK_TYPE_TB3))
            {
                family_template = &g_ui_template_tone_family_tb3;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_TONE, track_family, track_type, family_template);
        }
    }
}

const ui_page_t g_ui_page_template_dx7 = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .render = ui_template_page_render,
    .context = &g_ui_template_dx7_state,
};
