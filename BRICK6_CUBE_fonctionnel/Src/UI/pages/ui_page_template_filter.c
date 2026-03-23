#include "pages/ui_page_template_filter.h"

#include "mixer.h"
#include "param_store.h"
#include "ui_core.h"
#include "ui_template_page.h"

static ui_template_family_t g_ui_template_filter_family_audio = {
    .family_title = "FILTER",
    .nav_labels = { "MAIN", "-", "MOD", "-" },
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
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static ui_template_family_t g_ui_template_filter_family_monob = {
    .family_title = "FILTER",
    .nav_labels = { "MAIN", "ENV", "MOD", "-" },
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
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t *ui_page_template_filter_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_FILTER);
}

static ui_template_page_state_t g_ui_template_filter_state = {
    .family = 0,
    .family_resolver = ui_page_template_filter_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_filter_register_families(void)
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
            if ((track_family == UI_TRACK_FAMILY_SYNTH) && (track_type == UI_TRACK_TYPE_MONOB))
            {
                family_template = &g_ui_template_filter_family_monob;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_FILTER, track_family, track_type, family_template);
        }
    }
}

static ui_template_family_t *ui_page_template_filter_get_audio_family(void)
{
    return (ui_template_family_t *)ui_page_template_filter_resolve_family();
}

static void ui_page_template_filter_sync_family(void)
{
    ui_template_family_t *family = ui_page_template_filter_get_audio_family();
    if (family == 0)
    {
        return;
    }

    if (ui_get_track_type(ui_get_active_track()) == UI_TRACK_TYPE_MONOB)
    {
        family->nav_labels[0] = "MAIN";
        family->nav_labels[1] = "ENV";
        family->nav_labels[2] = "MOD";
        family->nav_labels[3] = "-";

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
        return;
    }

    const mixer_track_filter_type_t filter_type = (mixer_track_filter_type_t)((uint8_t)(param_store_get_active(PARAM_FILTER_TYPE) + 0.5f));
    /* Audio FILTER keeps a compact track-aware variant: EQ3 swaps to Low/Mid/High, biquad keeps Cutoff/Res, no ENV page. */
    const uint8_t is_eq3 = (filter_type == MIXER_TRACK_FILTER_EQ3) ? 1U : 0U;
    const uint8_t has_mod_page = ((filter_type == MIXER_TRACK_FILTER_LP_BI)
                               || (filter_type == MIXER_TRACK_FILTER_HP_BI)
                               || (filter_type == MIXER_TRACK_FILTER_BP_BI)) ? 1U : 0U;

    family->nav_labels[0] = "MAIN";
    family->nav_labels[1] = "-";
    family->nav_labels[2] = (has_mod_page != 0U) ? "MOD" : "-";
    family->nav_labels[3] = "-";

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
