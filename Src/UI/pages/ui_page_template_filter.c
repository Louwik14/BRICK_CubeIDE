#include "pages/ui_page_template_filter.h"

#include "mixer.h"
#include "param_store.h"
#include "ui_core.h"
#include "ui_template_page.h"

static ui_template_family_t g_ui_template_filter_family_audio = {
    .family_title = "COLORS",
    .nav_labels = { "MAIN", "ADSR", "-", "CRUNCH" },
    .subpages = {
        {
            .title = "MAIN",
            .param_bank = { .params = { PARAM_FILTER_TYPE, PARAM_FILTER_CUTOFF, PARAM_FILTER_RESONANCE, PARAM_FILTER_EG_AMT } },
        },
        {
            .title = "ADSR",
            .param_bank = { .params = { PARAM_FILTER_ATTACK, PARAM_FILTER_DECAY, PARAM_FILTER_SUSTAIN, PARAM_FILTER_RELEASE } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "CRUNCH",
            .param_bank = { .params = { PARAM_FILTER_DRIVE, PARAM_FILTER_DECIMATOR_BITS, PARAM_FILTER_DECIMATOR_RATE, PARAM_FILTER_DECIMATOR_RATE2 } },
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
            .param_bank = { .params = { PARAM_FILTER_DRIVE, PARAM_FILTER_DECIMATOR_BITS, PARAM_FILTER_DECIMATOR_RATE, PARAM_FILTER_DECIMATOR_RATE2 } },
        },
    },
    .default_subpage = 0U,
};


static const ui_template_family_t *ui_page_template_colors_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_COLORS);
}

static ui_template_family_t g_ui_template_vca_family = {
    .family_title = "VCA",
    .nav_labels = { "ADSR", "-", "-", "-" },
    .subpages = {
        {
            .title = "ADSR",
            .param_bank = { .params = { PARAM_VCA_ATTACK, PARAM_VCA_DECAY, PARAM_VCA_SUSTAIN, PARAM_VCA_RELEASE } },
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

static const ui_template_family_t *ui_page_template_vca_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_VCA);
}

static ui_template_page_state_t g_ui_template_filter_state = {
    .family = 0,
    .family_resolver = ui_page_template_colors_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

static ui_template_page_state_t g_ui_template_vca_state = {
    .family = 0,
    .family_resolver = ui_page_template_vca_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_colors_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; family++)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        if (track_family == UI_TRACK_FAMILY_MIDI)
        {
            continue;
        }
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

            ui_template_family_register(UI_TEMPLATE_FAMILY_COLORS, track_family, track_type, family_template);
        }
    }
}

void ui_page_template_vca_register_families(void)
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

            const ui_template_family_t *family_template = &g_ui_template_vca_family;
            if ((ui_track_family_is_engine(track_family) == 0U)
                    && !((ui_track_family_is_input(track_family) != 0U) && (track_type == UI_TRACK_TYPE_HYBRID)))
            {
                family_template = NULL;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_VCA, track_family, track_type, family_template);
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
    const uint8_t active_track = ui_get_active_track();
    const ui_track_family_t active_family = ui_get_track_family(active_track);
    const ui_track_type_t active_type = ui_get_track_type(active_track);
    if (family == 0)
    {
        return;
    }

    if (active_type == UI_TRACK_TYPE_MONOB)
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
        family->subpages[3].param_bank.params[1] = PARAM_FILTER_DECIMATOR_BITS;
        family->subpages[3].param_bank.params[2] = PARAM_FILTER_DECIMATOR_RATE;
        family->subpages[3].param_bank.params[3] = PARAM_FILTER_DECIMATOR_RATE2;
        return;
    }
    if (!ui_resolve_filter_target_track(&filter_target_track))
    {
        const uint8_t allow_dx7_colors_without_filter_target = ((active_family == UI_TRACK_FAMILY_SYNTH)
                                                              && (active_type == UI_TRACK_TYPE_DX7)) ? 1U : 0U;
        if (allow_dx7_colors_without_filter_target != 0U)
        {
            /*
             * DX7 keeps COLORS pages even when mixer filter target resolution is
             * temporarily unavailable in runtime binding (track-aware fallback).
             */
        }
        else
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
    }

    const mixer_track_filter_type_t filter_type = (mixer_track_filter_type_t)((uint8_t)(param_store_get_active(PARAM_FILTER_TYPE) + 0.5f));
    const uint8_t is_eq3 = (filter_type == MIXER_TRACK_FILTER_EQ3) ? 1U : 0U;
    const uint8_t has_adsr_page = ((filter_type == MIXER_TRACK_FILTER_LP_BI)
                                || (filter_type == MIXER_TRACK_FILTER_HP_BI)
                                || (filter_type == MIXER_TRACK_FILTER_BP_BI)) ? 1U : 0U;

    family->nav_labels[0] = "MAIN";
    family->nav_labels[1] = (has_adsr_page != 0U) ? "ADSR" : "-";
    family->nav_labels[2] = "-";
    family->nav_labels[3] = "CRUNCH";

    family->subpages[0].title = "MAIN";
    family->subpages[0].param_bank.params[0] = PARAM_FILTER_TYPE;
    family->subpages[0].param_bank.params[1] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_LOW : PARAM_FILTER_CUTOFF;
    family->subpages[0].param_bank.params[2] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_MID : PARAM_FILTER_RESONANCE;
    family->subpages[0].param_bank.params[3] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_HIGH : ((has_adsr_page != 0U) ? PARAM_FILTER_EG_AMT : PARAM_COUNT);

    family->subpages[1].title = (has_adsr_page != 0U) ? "ADSR" : "-";
    family->subpages[1].param_bank.params[0] = (has_adsr_page != 0U) ? PARAM_FILTER_ATTACK : PARAM_COUNT;
    family->subpages[1].param_bank.params[1] = (has_adsr_page != 0U) ? PARAM_FILTER_DECAY : PARAM_COUNT;
    family->subpages[1].param_bank.params[2] = (has_adsr_page != 0U) ? PARAM_FILTER_SUSTAIN : PARAM_COUNT;
    family->subpages[1].param_bank.params[3] = (has_adsr_page != 0U) ? PARAM_FILTER_RELEASE : PARAM_COUNT;

    family->subpages[2].title = "-";
    family->subpages[2].param_bank.params[0] = PARAM_COUNT;
    family->subpages[2].param_bank.params[1] = PARAM_COUNT;
    family->subpages[2].param_bank.params[2] = PARAM_COUNT;
    family->subpages[2].param_bank.params[3] = PARAM_COUNT;

    family->subpages[3].title = "CRUNCH";
    family->subpages[3].param_bank.params[0] = PARAM_FILTER_DRIVE;
    family->subpages[3].param_bank.params[1] = PARAM_FILTER_DECIMATOR_BITS;
    family->subpages[3].param_bank.params[2] = PARAM_FILTER_DECIMATOR_RATE;
    family->subpages[3].param_bank.params[3] = PARAM_FILTER_DECIMATOR_RATE2;
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

static void ui_page_template_colors_sync_active_context(void)
{
    ui_page_template_colors_sync_family();
    ui_template_page_select_subpage(&g_ui_template_filter_state, g_ui_template_filter_state.active_subpage);
    ui_template_page_sync_active_track_context();
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
    .sync_active_context = ui_page_template_colors_sync_active_context,
    .render = ui_page_template_colors_render,
    .context = &g_ui_template_filter_state,
};

const ui_page_t g_ui_page_template_vca = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_vca_state,
};
