#include "pages/ui_page_template_filter.h"

#include "mixer.h"
#include "param_store.h"
#include "ui_core.h"
#include "ui_template_page.h"

static ui_template_family_t g_ui_template_filter_family_audio = {
    .family_title = "COLORS",
    .nav_labels = { "MAIN", "-", "MOD", "CRUNCH" },
    .subpages = {
        {
            .title = "MAIN",
            .param_bank = { .params = { PARAM_FILTER_TYPE, PARAM_FILTER_CUTOFF, PARAM_FILTER_RESONANCE, PARAM_COUNT } },
        },
        {
            .title = "MOD",
            .param_bank = { .params = { PARAM_FILTER_KEYTRK, PARAM_FILTER_ENVRST, PARAM_FILTER_ENVDLY, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "CRUNCH",
            .param_bank = { .params = { PARAM_FILTER_DRIVE, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static ui_template_family_t g_ui_template_filter_family_monob = {
    .family_title = "COLORS",
    .nav_labels = { "MAIN", "ENV", "MOD", "CRUNCH" },
    .subpages = {
        {
            .title = "MAIN",
            .param_bank = { .params = { PARAM_MONOB_FILTER_TYPE, PARAM_MONOB_FILTER_CUTOFF, PARAM_MONOB_FILTER_RESONANCE, PARAM_MONOB_FILTER_EG_AMT } },
        },
        {
            .title = "ENV",
            .param_bank = { .params = { PARAM_MONOB_FILTER_ATTACK, PARAM_MONOB_FILTER_DECAY, PARAM_MONOB_FILTER_SUSTAIN, PARAM_MONOB_FILTER_RELEASE } },
        },
        {
            .title = "MOD",
            .param_bank = { .params = { PARAM_MONOB_FILTER_KEYTRK, PARAM_MONOB_FILTER_ENVRST, PARAM_MONOB_FILTER_ENVDLY, PARAM_COUNT } },
        },
        {
            .title = "CRUNCH",
            .param_bank = { .params = { PARAM_FILTER_DRIVE, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static ui_template_family_t g_ui_template_filter_family_tb3 = {
    .family_title = "COLORS",
    .nav_labels = { "MAIN", "-", "-", "CRUNCH" },
    .subpages = {
        {
            .title = "MAIN",
            .param_bank = { .params = { PARAM_TB3_CUTOFF, PARAM_TB3_RESONANCE, PARAM_TB3_ENV_MOD, PARAM_TB3_DECAY } },
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
            .title = "CRUNCH",
            .param_bank = { .params = { PARAM_FILTER_DRIVE, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t *ui_page_template_colors_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_COLORS);
}

static ui_template_page_state_t g_ui_template_filter_state = {
    .family = 0,
    .family_resolver = ui_page_template_colors_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_colors_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; family++)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        for (uint8_t type = 0U; type < (uint8_t)UI_TRACK_TYPE_COUNT; type++)
        {
            const ui_track_type_t track_type = (ui_track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type))
            {
                continue;
            }

            const ui_template_family_t *family_template = &g_ui_template_filter_family_audio;
            if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_MONOB))
            {
                family_template = &g_ui_template_filter_family_monob;
            }
            else if ((ui_track_family_is_engine(track_family) != 0) && (track_type == UI_TRACK_TYPE_TB3))
            {
                family_template = &g_ui_template_filter_family_tb3;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_COLORS, track_family, track_type, family_template);
        }
    }
}

static ui_template_family_t *ui_page_template_colors_get_audio_family(void)
{
    return (ui_template_family_t *)ui_page_template_colors_resolve_family();
}

static void ui_page_template_colors_sync_family(void)
{
    ui_template_family_t *family = ui_page_template_colors_get_audio_family();
    uint8_t filter_target_track = 0U;
    if (family == 0)
    {
        return;
    }

    if (ui_get_track_type(ui_get_active_track()) == UI_TRACK_TYPE_MONOB)
    {
        family->nav_labels[0] = "MAIN";
        family->nav_labels[1] = "ENV";
        family->nav_labels[2] = "MOD";
        family->nav_labels[3] = "CRUNCH";

        family->subpages[0].title = "MAIN";
        family->subpages[0].param_bank.params[0] = PARAM_MONOB_FILTER_TYPE;
        family->subpages[0].param_bank.params[1] = PARAM_MONOB_FILTER_CUTOFF;
        family->subpages[0].param_bank.params[2] = PARAM_MONOB_FILTER_RESONANCE;
        family->subpages[0].param_bank.params[3] = PARAM_MONOB_FILTER_EG_AMT;

        family->subpages[1].title = "ENV";
        family->subpages[1].param_bank.params[0] = PARAM_MONOB_FILTER_ATTACK;
        family->subpages[1].param_bank.params[1] = PARAM_MONOB_FILTER_DECAY;
        family->subpages[1].param_bank.params[2] = PARAM_MONOB_FILTER_SUSTAIN;
        family->subpages[1].param_bank.params[3] = PARAM_MONOB_FILTER_RELEASE;

        family->subpages[2].title = "MOD";
        family->subpages[2].param_bank.params[0] = PARAM_MONOB_FILTER_KEYTRK;
        family->subpages[2].param_bank.params[1] = PARAM_MONOB_FILTER_ENVRST;
        family->subpages[2].param_bank.params[2] = PARAM_MONOB_FILTER_ENVDLY;
        family->subpages[2].param_bank.params[3] = PARAM_COUNT;

        family->subpages[3].title = "CRUNCH";
        family->subpages[3].param_bank.params[0] = PARAM_FILTER_DRIVE;
        family->subpages[3].param_bank.params[1] = PARAM_COUNT;
        family->subpages[3].param_bank.params[2] = PARAM_COUNT;
        family->subpages[3].param_bank.params[3] = PARAM_COUNT;
        return;
    }
    if (ui_get_track_family(ui_get_active_track()) == UI_TRACK_FAMILY_DRUM)
    {
        family->nav_labels[0] = "MAIN";
        family->nav_labels[1] = "TONE";
        family->nav_labels[2] = "-";
        family->nav_labels[3] = "-";

        family->subpages[0].title = "MAIN";
        family->subpages[1].title = "TONE";
        family->subpages[2].title = "-";
        family->subpages[3].title = "-";
        family->subpages[2].param_bank.params[0] = PARAM_COUNT;
        family->subpages[2].param_bank.params[1] = PARAM_COUNT;
        family->subpages[2].param_bank.params[2] = PARAM_COUNT;
        family->subpages[2].param_bank.params[3] = PARAM_COUNT;
        family->subpages[3].param_bank.params[0] = PARAM_COUNT;
        family->subpages[3].param_bank.params[1] = PARAM_COUNT;
        family->subpages[3].param_bank.params[2] = PARAM_COUNT;
        family->subpages[3].param_bank.params[3] = PARAM_COUNT;

        switch (ui_get_track_type(ui_get_active_track()))
        {
            case UI_TRACK_TYPE_DRUM_TRX_BD:
                family->subpages[0].param_bank.params[0] = PARAM_DRUM_TRX_BD_NOISE;
                family->subpages[0].param_bank.params[1] = PARAM_DRUM_TRX_BD_HARMONICS;
                family->subpages[0].param_bank.params[2] = PARAM_DRUM_TRX_BD_DRIVE;
                family->subpages[0].param_bank.params[3] = PARAM_COUNT;
                family->subpages[1].param_bank.params[0] = PARAM_COUNT;
                family->subpages[1].param_bank.params[1] = PARAM_COUNT;
                family->subpages[1].param_bank.params[2] = PARAM_COUNT;
                family->subpages[1].param_bank.params[3] = PARAM_COUNT;
                break;
            case UI_TRACK_TYPE_DRUM_TRX_CLAVES:
                family->subpages[0].param_bank.params[0] = PARAM_DRUM_TRX_CLAVES_BALANCE;
                family->subpages[0].param_bank.params[1] = PARAM_DRUM_TRX_CLAVES_DRIVE;
                family->subpages[0].param_bank.params[2] = PARAM_COUNT;
                family->subpages[0].param_bank.params[3] = PARAM_COUNT;
                family->subpages[1].param_bank.params[0] = PARAM_COUNT;
                family->subpages[1].param_bank.params[1] = PARAM_COUNT;
                family->subpages[1].param_bank.params[2] = PARAM_COUNT;
                family->subpages[1].param_bank.params[3] = PARAM_COUNT;
                break;
            case UI_TRACK_TYPE_DRUM_TRX_HIHAT:
                family->subpages[0].param_bank.params[0] = PARAM_DRUM_TRX_HIHAT_METAL;
                family->subpages[0].param_bank.params[1] = PARAM_DRUM_TRX_HIHAT_HP_TONE;
                family->subpages[0].param_bank.params[2] = PARAM_DRUM_TRX_HIHAT_LP_TONE;
                family->subpages[0].param_bank.params[3] = PARAM_COUNT;
                family->subpages[1].param_bank.params[0] = PARAM_COUNT;
                family->subpages[1].param_bank.params[1] = PARAM_COUNT;
                family->subpages[1].param_bank.params[2] = PARAM_COUNT;
                family->subpages[1].param_bank.params[3] = PARAM_COUNT;
                break;
            case UI_TRACK_TYPE_DRUM_TRX_SNARE:
                family->subpages[0].param_bank.params[0] = PARAM_DRUM_TRX_SNARE_SNAP;
                family->subpages[0].param_bank.params[1] = PARAM_DRUM_TRX_SNARE_NOISE;
                family->subpages[0].param_bank.params[2] = PARAM_DRUM_TRX_SNARE_TONE_MIX;
                family->subpages[0].param_bank.params[3] = PARAM_DRUM_TRX_SNARE_DRIVE;
                family->subpages[1].param_bank.params[0] = PARAM_COUNT;
                family->subpages[1].param_bank.params[1] = PARAM_COUNT;
                family->subpages[1].param_bank.params[2] = PARAM_COUNT;
                family->subpages[1].param_bank.params[3] = PARAM_COUNT;
                break;
            case UI_TRACK_TYPE_DRUM_FM_KICK:
                family->subpages[0].param_bank.params[0] = PARAM_DRUM_FM_KICK_FEEDBACK;
                family->subpages[0].param_bank.params[1] = PARAM_COUNT;
                family->subpages[0].param_bank.params[2] = PARAM_COUNT;
                family->subpages[0].param_bank.params[3] = PARAM_COUNT;
                family->subpages[1].param_bank.params[0] = PARAM_COUNT;
                family->subpages[1].param_bank.params[1] = PARAM_COUNT;
                family->subpages[1].param_bank.params[2] = PARAM_COUNT;
                family->subpages[1].param_bank.params[3] = PARAM_COUNT;
                break;
            case UI_TRACK_TYPE_DRUM_FM_SNARE:
                family->subpages[0].param_bank.params[0] = PARAM_DRUM_FM_SNARE_NOISE;
                family->subpages[0].param_bank.params[1] = PARAM_DRUM_FM_SNARE_HP_TONE;
                family->subpages[0].param_bank.params[2] = PARAM_COUNT;
                family->subpages[0].param_bank.params[3] = PARAM_COUNT;
                family->subpages[1].param_bank.params[0] = PARAM_COUNT;
                family->subpages[1].param_bank.params[1] = PARAM_COUNT;
                family->subpages[1].param_bank.params[2] = PARAM_COUNT;
                family->subpages[1].param_bank.params[3] = PARAM_COUNT;
                break;
            case UI_TRACK_TYPE_DRUM_FM_RIMSHOT:
                family->subpages[0].param_bank.params[0] = PARAM_DRUM_FM_RIMSHOT_BODY_MIX;
                family->subpages[0].param_bank.params[1] = PARAM_DRUM_FM_RIMSHOT_HP_TONE;
                family->subpages[0].param_bank.params[2] = PARAM_COUNT;
                family->subpages[0].param_bank.params[3] = PARAM_COUNT;
                family->subpages[1].param_bank.params[0] = PARAM_COUNT;
                family->subpages[1].param_bank.params[1] = PARAM_COUNT;
                family->subpages[1].param_bank.params[2] = PARAM_COUNT;
                family->subpages[1].param_bank.params[3] = PARAM_COUNT;
                break;
            case UI_TRACK_TYPE_DRUM_FM_CLAP:
                family->subpages[0].param_bank.params[0] = PARAM_DRUM_FM_CLAP_HP_TONE;
                family->subpages[0].param_bank.params[1] = PARAM_DRUM_FM_CLAP_FEEDBACK;
                family->subpages[0].param_bank.params[2] = PARAM_COUNT;
                family->subpages[0].param_bank.params[3] = PARAM_COUNT;
                family->subpages[1].param_bank.params[0] = PARAM_COUNT;
                family->subpages[1].param_bank.params[1] = PARAM_COUNT;
                family->subpages[1].param_bank.params[2] = PARAM_COUNT;
                family->subpages[1].param_bank.params[3] = PARAM_COUNT;
                break;
            case UI_TRACK_TYPE_DRUM_FM_COWBELL:
                family->subpages[0].param_bank.params[0] = PARAM_DRUM_FM_COWBELL_FEEDBACK;
                family->subpages[0].param_bank.params[1] = PARAM_DRUM_FM_COWBELL_ENV_MIX;
                family->subpages[0].param_bank.params[2] = PARAM_COUNT;
                family->subpages[0].param_bank.params[3] = PARAM_COUNT;
                family->subpages[1].param_bank.params[0] = PARAM_COUNT;
                family->subpages[1].param_bank.params[1] = PARAM_COUNT;
                family->subpages[1].param_bank.params[2] = PARAM_COUNT;
                family->subpages[1].param_bank.params[3] = PARAM_COUNT;
                break;
            case UI_TRACK_TYPE_DRUM_FM_CYMBAL:
                family->subpages[0].param_bank.params[0] = PARAM_DRUM_FM_CYMBAL_HP_TONE;
                family->subpages[0].param_bank.params[1] = PARAM_DRUM_FM_CYMBAL_FEEDBACK;
                family->subpages[0].param_bank.params[2] = PARAM_COUNT;
                family->subpages[0].param_bank.params[3] = PARAM_COUNT;
                family->subpages[1].param_bank.params[0] = PARAM_COUNT;
                family->subpages[1].param_bank.params[1] = PARAM_COUNT;
                family->subpages[1].param_bank.params[2] = PARAM_COUNT;
                family->subpages[1].param_bank.params[3] = PARAM_COUNT;
                break;
            default:
                break;
        }
        return;
    }
    if (ui_get_track_type(ui_get_active_track()) == UI_TRACK_TYPE_TB3)
    {
        if (g_ui_template_filter_state.active_subpage != 0U)
        {
            g_ui_template_filter_state.active_subpage = 0U;
        }

        family->nav_labels[0] = "MAIN";
        family->nav_labels[1] = "-";
        family->nav_labels[2] = "-";
        family->nav_labels[3] = "CRUNCH";

        family->subpages[0].title = "MAIN";
        family->subpages[0].param_bank.params[0] = PARAM_TB3_CUTOFF;
        family->subpages[0].param_bank.params[1] = PARAM_TB3_RESONANCE;
        family->subpages[0].param_bank.params[2] = PARAM_TB3_ENV_MOD;
        family->subpages[0].param_bank.params[3] = PARAM_TB3_DECAY;

        family->subpages[1].title = "-";
        family->subpages[1].param_bank.params[0] = PARAM_COUNT;
        family->subpages[1].param_bank.params[1] = PARAM_COUNT;
        family->subpages[1].param_bank.params[2] = PARAM_COUNT;
        family->subpages[1].param_bank.params[3] = PARAM_COUNT;

        family->subpages[2].title = "-";
        family->subpages[2].param_bank.params[0] = PARAM_COUNT;
        family->subpages[2].param_bank.params[1] = PARAM_COUNT;
        family->subpages[2].param_bank.params[2] = PARAM_COUNT;
        family->subpages[2].param_bank.params[3] = PARAM_COUNT;

        family->subpages[3].title = "CRUNCH";
        family->subpages[3].param_bank.params[0] = PARAM_FILTER_DRIVE;
        family->subpages[3].param_bank.params[1] = PARAM_COUNT;
        family->subpages[3].param_bank.params[2] = PARAM_COUNT;
        family->subpages[3].param_bank.params[3] = PARAM_COUNT;
        return;
    }

    if (!ui_resolve_filter_target_track(&filter_target_track))
    {
        family->nav_labels[0] = "-";
        family->nav_labels[1] = "-";
        family->nav_labels[2] = "-";
        family->nav_labels[3] = "-";

        family->subpages[0].title = "N/A";
        family->subpages[0].param_bank.params[0] = PARAM_COUNT;
        family->subpages[0].param_bank.params[1] = PARAM_COUNT;
        family->subpages[0].param_bank.params[2] = PARAM_COUNT;
        family->subpages[0].param_bank.params[3] = PARAM_COUNT;

        family->subpages[1].title = "-";
        family->subpages[1].param_bank.params[0] = PARAM_COUNT;
        family->subpages[1].param_bank.params[1] = PARAM_COUNT;
        family->subpages[1].param_bank.params[2] = PARAM_COUNT;
        family->subpages[1].param_bank.params[3] = PARAM_COUNT;

        family->subpages[2].title = "-";
        family->subpages[2].param_bank.params[0] = PARAM_COUNT;
        family->subpages[2].param_bank.params[1] = PARAM_COUNT;
        family->subpages[2].param_bank.params[2] = PARAM_COUNT;
        family->subpages[2].param_bank.params[3] = PARAM_COUNT;

        family->subpages[3].title = "-";
        family->subpages[3].param_bank.params[0] = PARAM_COUNT;
        family->subpages[3].param_bank.params[1] = PARAM_COUNT;
        family->subpages[3].param_bank.params[2] = PARAM_COUNT;
        family->subpages[3].param_bank.params[3] = PARAM_COUNT;
        return;
    }

    const mixer_track_filter_type_t filter_type = (mixer_track_filter_type_t)((uint8_t)(param_store_get_active(PARAM_FILTER_TYPE) + 0.5f));
    const uint8_t is_eq3 = (filter_type == MIXER_TRACK_FILTER_EQ3) ? 1U : 0U;
    const uint8_t is_input_audio = (ui_get_track_type(ui_get_active_track()) == UI_TRACK_TYPE_AUDIO) ? 1U : 0U;
    const uint8_t has_mod_page = (((filter_type == MIXER_TRACK_FILTER_LP_BI)
                                || (filter_type == MIXER_TRACK_FILTER_HP_BI)
                                || (filter_type == MIXER_TRACK_FILTER_BP_BI))
                                && (is_input_audio == 0U)) ? 1U : 0U;

    family->nav_labels[0] = "MAIN";
    family->nav_labels[1] = "-";
    family->nav_labels[2] = (has_mod_page != 0U) ? "MOD" : "-";
    family->nav_labels[3] = "CRUNCH";

    family->subpages[0].title = "MAIN";
    family->subpages[0].param_bank.params[0] = PARAM_FILTER_TYPE;
    family->subpages[0].param_bank.params[1] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_LOW : PARAM_FILTER_CUTOFF;
    family->subpages[0].param_bank.params[2] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_MID : PARAM_FILTER_RESONANCE;
    family->subpages[0].param_bank.params[3] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_HIGH : PARAM_COUNT;

    family->subpages[1].title = "-";
    family->subpages[1].param_bank.params[0] = PARAM_COUNT;
    family->subpages[1].param_bank.params[1] = PARAM_COUNT;
    family->subpages[1].param_bank.params[2] = PARAM_COUNT;
    family->subpages[1].param_bank.params[3] = PARAM_COUNT;

    family->subpages[2].title = (has_mod_page != 0U) ? "MOD" : "-";
    family->subpages[2].param_bank.params[0] = (has_mod_page != 0U) ? PARAM_FILTER_KEYTRK : PARAM_COUNT;
    family->subpages[2].param_bank.params[1] = (has_mod_page != 0U) ? PARAM_FILTER_ENVRST : PARAM_COUNT;
    family->subpages[2].param_bank.params[2] = (has_mod_page != 0U) ? PARAM_FILTER_ENVDLY : PARAM_COUNT;
    family->subpages[2].param_bank.params[3] = PARAM_COUNT;

    family->subpages[3].title = "CRUNCH";
    family->subpages[3].param_bank.params[0] = PARAM_FILTER_DRIVE;
    family->subpages[3].param_bank.params[1] = PARAM_COUNT;
    family->subpages[3].param_bank.params[2] = PARAM_COUNT;
    family->subpages[3].param_bank.params[3] = PARAM_COUNT;
}

static void ui_page_template_colors_enter(void)
{
    ui_page_template_colors_sync_family();
    ui_template_page_enter();
}

static void ui_page_template_colors_handle_event(const ui_event_t *ev)
{
    ui_page_template_colors_sync_family();
    ui_template_page_handle_event(ev);
    ui_page_template_colors_sync_family();
    ui_template_page_select_subpage(&g_ui_template_filter_state, g_ui_template_filter_state.active_subpage);
}

static void ui_page_template_colors_tick(void)
{
    ui_page_template_colors_sync_family();
    ui_template_page_select_subpage(&g_ui_template_filter_state, g_ui_template_filter_state.active_subpage);
    ui_template_page_tick();
}

static void ui_page_template_colors_render(void)
{
    ui_page_template_colors_sync_family();
    ui_template_page_render();
}

const ui_page_t g_ui_page_template_colors = {
    .enter = ui_page_template_colors_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_page_template_colors_handle_event,
    .tick = ui_page_template_colors_tick,
    .render = ui_page_template_colors_render,
    .context = &g_ui_template_filter_state,
};
