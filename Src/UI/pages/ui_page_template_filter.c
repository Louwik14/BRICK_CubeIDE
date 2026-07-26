#include "pages/ui_page_template_filter.h"

#include "mixer.h"
#include "param_store.h"
#include "ui_core.h"
#include "ui_template_page.h"
#include "Core/track_runtime.h"

static ui_template_family_t g_ui_template_filter_family_audio = {
    .family_title = "ENV",
    .nav_labels = { "FILTER", "ADSR", "VCA", "ENV 3" },
    .subpages = {
        {
            .title = "FILTER",
            .param_bank = { .params = { PARAM_FILTER_CUTOFF, PARAM_FILTER_RESONANCE, PARAM_FILTER_EG_AMT, PARAM_FILTER_TYPE } },
        },
        {
            .title = "ADSR",
            .param_bank = { .params = { PARAM_FILTER_ATTACK, PARAM_FILTER_DECAY, PARAM_FILTER_SUSTAIN, PARAM_FILTER_RELEASE } },
        },
        {
            .title = "VCA",
            .param_bank = { .params = { PARAM_VCA_ATTACK, PARAM_VCA_DECAY, PARAM_VCA_SUSTAIN, PARAM_VCA_RELEASE } },
        },
        {
            .title = "ENV 3",
            .param_bank = { .params = { PARAM_ENV3_ATTACK, PARAM_ENV3_DECAY, PARAM_ENV3_SUSTAIN, PARAM_ENV3_RELEASE } },
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

static uint8_t ui_page_template_subpage_matches_adsr(const ui_template_subpage_t *subpage,
                                                     param_id_t attack,
                                                     param_id_t decay,
                                                     param_id_t sustain,
                                                     param_id_t release)
{
    return (uint8_t)((subpage != 0)
            && (subpage->param_bank.params[0] == attack)
            && (subpage->param_bank.params[1] == decay)
            && (subpage->param_bank.params[2] == sustain)
            && (subpage->param_bank.params[3] == release));
}

static ui_template_custom_widget_kind_t ui_page_template_filter_pick_custom_widget(uint8_t slot,
                                                                                   const ui_template_subpage_t *subpage,
                                                                                   param_id_t id)
{
    if ((subpage != 0)
            && (subpage->param_bank.params[3] == PARAM_FILTER_TYPE)
            && (slot == 3U)
            && (id == PARAM_FILTER_TYPE))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_FILTER_TYPE;
    }

    if ((subpage != 0)
            && (subpage->param_bank.params[3] == PARAM_FILTER_TYPE)
            && (slot == 0U)
            && (id == PARAM_FILTER_CUTOFF))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP;
    }

    if ((subpage != 0)
            && (subpage->param_bank.params[3] == PARAM_FILTER_TYPE)
            && (slot == 1U)
            && (id == PARAM_FILTER_RESONANCE))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP;
    }

    if (((id == PARAM_FILTER_ATTACK)
            || (id == PARAM_FILTER_DECAY)
            || (id == PARAM_FILTER_SUSTAIN)
            || (id == PARAM_FILTER_RELEASE))
            && (ui_page_template_subpage_matches_adsr(subpage,
                                                      PARAM_FILTER_ATTACK,
                                                      PARAM_FILTER_DECAY,
                                                      PARAM_FILTER_SUSTAIN,
                                                      PARAM_FILTER_RELEASE) != 0U))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_ADSR_FILTER;
    }

    if (((id == PARAM_VCA_ATTACK)
            || (id == PARAM_VCA_DECAY)
            || (id == PARAM_VCA_SUSTAIN)
            || (id == PARAM_VCA_RELEASE))
            && (ui_page_template_subpage_matches_adsr(subpage,
                                                      PARAM_VCA_ATTACK,
                                                      PARAM_VCA_DECAY,
                                                      PARAM_VCA_SUSTAIN,
                                                      PARAM_VCA_RELEASE) != 0U))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_ADSR_VCA;
    }

    if (((id == PARAM_ENV3_ATTACK)
            || (id == PARAM_ENV3_DECAY)
            || (id == PARAM_ENV3_SUSTAIN)
            || (id == PARAM_ENV3_RELEASE))
            && (ui_page_template_subpage_matches_adsr(subpage,
                                                      PARAM_ENV3_ATTACK,
                                                      PARAM_ENV3_DECAY,
                                                      PARAM_ENV3_SUSTAIN,
                                                      PARAM_ENV3_RELEASE) != 0U))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_ADSR_ENV3;
    }

    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static ui_template_custom_widget_kind_t ui_page_template_vca_pick_custom_widget(uint8_t slot,
                                                                                const ui_template_subpage_t *subpage,
                                                                                param_id_t id)
{
    (void)slot;

    if (((id == PARAM_VCA_ATTACK)
            || (id == PARAM_VCA_DECAY)
            || (id == PARAM_VCA_SUSTAIN)
            || (id == PARAM_VCA_RELEASE))
            && (ui_page_template_subpage_matches_adsr(subpage,
                                                      PARAM_VCA_ATTACK,
                                                      PARAM_VCA_DECAY,
                                                      PARAM_VCA_SUSTAIN,
                                                      PARAM_VCA_RELEASE) != 0U))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_ADSR_VCA;
    }

    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static ui_template_page_state_t g_ui_template_filter_state = {
    .family = 0,
    .family_resolver = ui_page_template_colors_resolve_family,
    .custom_widget_picker = ui_page_template_filter_pick_custom_widget,
    .active_subpage = 0U,
    .has_visited = 0U,
};

typedef struct
{
    uint8_t valid;
    uint8_t active_track;
    uint8_t family;
    uint8_t type;
    uint8_t filter_type_ui;
    uint32_t runtime_track_revision;
} ui_page_template_colors_sync_cache_t;

static ui_page_template_colors_sync_cache_t g_ui_template_colors_sync_cache = { 0U };

static ui_template_page_state_t g_ui_template_vca_state = {
    .family = 0,
    .family_resolver = ui_page_template_vca_resolve_family,
    .custom_widget_picker = ui_page_template_vca_pick_custom_widget,
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

            ui_template_family_register(UI_TEMPLATE_FAMILY_COLORS, track_family, track_type, &g_ui_template_filter_family_audio);
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

static uint8_t ui_page_template_colors_sync_should_recompute(uint8_t active_track,
                                                             ui_track_family_t active_family,
                                                             ui_track_type_t active_type)
{
    /* Consumer-edge refresh: revision checks use a refreshed projection; the revision is a coherence guard only. */
    track_runtime_refresh_track(active_track);
    const uint32_t runtime_track_revision = track_runtime_get_track_revision(active_track);
    const uint8_t filter_type_ui = (uint8_t)(param_store_get_active(PARAM_FILTER_TYPE) + 0.5f);

    if ((g_ui_template_colors_sync_cache.valid != 0U)
            && (g_ui_template_colors_sync_cache.active_track == active_track)
            && (g_ui_template_colors_sync_cache.family == (uint8_t)active_family)
            && (g_ui_template_colors_sync_cache.type == (uint8_t)active_type)
            && (g_ui_template_colors_sync_cache.filter_type_ui == filter_type_ui)
            && (g_ui_template_colors_sync_cache.runtime_track_revision == runtime_track_revision))
    {
        return 0U;
    }

    g_ui_template_colors_sync_cache.valid = 1U;
    g_ui_template_colors_sync_cache.active_track = active_track;
    g_ui_template_colors_sync_cache.family = (uint8_t)active_family;
    g_ui_template_colors_sync_cache.type = (uint8_t)active_type;
    g_ui_template_colors_sync_cache.filter_type_ui = filter_type_ui;
    g_ui_template_colors_sync_cache.runtime_track_revision = runtime_track_revision;
    return 1U;
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

    if (ui_page_template_colors_sync_should_recompute(active_track, active_family, active_type) == 0U)
    {
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
    const uint8_t has_adsr_page = ((filter_type == MIXER_TRACK_FILTER_LP_BI)
                                || (filter_type == MIXER_TRACK_FILTER_HP_BI)
                                || (filter_type == MIXER_TRACK_FILTER_BP_BI)) ? 1U : 0U;
    const uint8_t has_vca_page =
        (uint8_t)((track_runtime_is_ui_ensemble_available(active_track, TRACK_RUNTIME_UI_ENSEMBLE_VCA) != 0U)
                && (ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_VCA) != 0));

    family->nav_labels[0] = "FILTER";
    family->nav_labels[1] = (has_adsr_page != 0U) ? "ADSR" : "-";
    family->nav_labels[2] = (has_vca_page != 0U) ? "VCA" : "-";
    family->nav_labels[3] = "ENV 3";

    family->subpages[0].title = "FILTER";
    family->subpages[0].param_bank.params[0] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_LOW : PARAM_FILTER_CUTOFF;
    family->subpages[0].param_bank.params[1] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_MID : PARAM_FILTER_RESONANCE;
    family->subpages[0].param_bank.params[2] = (is_eq3 != 0U) ? PARAM_FILTER_EQ_HIGH : ((has_adsr_page != 0U) ? PARAM_FILTER_EG_AMT : PARAM_COUNT);
    family->subpages[0].param_bank.params[3] = PARAM_FILTER_TYPE;

    family->subpages[1].title = (has_adsr_page != 0U) ? "ADSR" : "-";
    family->subpages[1].param_bank.params[0] = (has_adsr_page != 0U) ? PARAM_FILTER_ATTACK : PARAM_COUNT;
    family->subpages[1].param_bank.params[1] = (has_adsr_page != 0U) ? PARAM_FILTER_DECAY : PARAM_COUNT;
    family->subpages[1].param_bank.params[2] = (has_adsr_page != 0U) ? PARAM_FILTER_SUSTAIN : PARAM_COUNT;
    family->subpages[1].param_bank.params[3] = (has_adsr_page != 0U) ? PARAM_FILTER_RELEASE : PARAM_COUNT;

    family->subpages[2].title = (has_vca_page != 0U) ? "VCA" : "-";
    family->subpages[2].param_bank.params[0] = (has_vca_page != 0U) ? PARAM_VCA_ATTACK : PARAM_COUNT;
    family->subpages[2].param_bank.params[1] = (has_vca_page != 0U) ? PARAM_VCA_DECAY : PARAM_COUNT;
    family->subpages[2].param_bank.params[2] = (has_vca_page != 0U) ? PARAM_VCA_SUSTAIN : PARAM_COUNT;
    family->subpages[2].param_bank.params[3] = (has_vca_page != 0U) ? PARAM_VCA_RELEASE : PARAM_COUNT;

    family->subpages[3].title = "ENV 3";
    family->subpages[3].param_bank.params[0] = PARAM_ENV3_ATTACK;
    family->subpages[3].param_bank.params[1] = PARAM_ENV3_DECAY;
    family->subpages[3].param_bank.params[2] = PARAM_ENV3_SUSTAIN;
    family->subpages[3].param_bank.params[3] = PARAM_ENV3_RELEASE;
}

static void ui_page_template_colors_enter(void)
{
    ui_page_template_colors_sync_family();
    ui_template_page_enter();
}

static void ui_page_template_colors_handle_event(const ui_event_t *ev)
{
    ui_template_page_handle_event(ev);
    ui_page_template_colors_sync_family();
    ui_template_page_select_subpage(&g_ui_template_filter_state, g_ui_template_filter_state.active_subpage);
}

static void ui_page_template_colors_tick(void)
{
    ui_page_template_colors_sync_family();
    ui_template_page_tick();
}

static void ui_page_template_colors_sync_active_context(void)
{
    ui_page_template_colors_sync_family();
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
