#include "pages/ui_page_template_env.h"

#include "param_store.h"
#include "ui_core.h"
#include "ui_navigation.h"
#include "ui_template_page.h"
#include "Track/track_runtime.h"

static uint8_t g_ui_template_env_subset = 0U;

static ui_template_family_t g_ui_template_env_family_audio = {
    .family_title = "ENV 1/2",
    .nav_labels = { "FILTER", "ADSR", "VCA", "ENV 3" },
    .subpages = {
        {
            .title = "FILTER",
            .param_bank = { .params = { PARAM_FILTER_CUTOFF, PARAM_FILTER_RESONANCE, PARAM_FILTER_EG_AMT, PARAM_FILTER_MORPH } },
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

static const ui_template_family_t g_ui_template_env_family_retrig = {
    .family_title = "ENV 2/2",
    .nav_labels = { "MODE", "-", "-", "-" },
    .subpages = {
        {
            .title = "MODE",
            .param_bank = { .params = { PARAM_ENV_RETRIG_FILTER, PARAM_ENV_RETRIG_VCA, PARAM_ENV_RETRIG_MOD, PARAM_FILTER_MODE } },
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

static const ui_template_family_t g_ui_template_env_family_group = {
    .family_title = "ENV",
    .nav_labels = { "ENV 3", "-", "-", "-" },
    .subpages = {
        { .title = "ENV 3", .param_bank = { .params = { PARAM_ENV3_ATTACK, PARAM_ENV3_DECAY, PARAM_ENV3_SUSTAIN, PARAM_ENV3_RELEASE } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_env_family_group_retrig = {
    .family_title = "ENV 2/2",
    .nav_labels = { "MODE", "-", "-", "-" },
    .subpages = {
        { .title = "MODE", .param_bank = { .params = { PARAM_ENV_RETRIG_MOD, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t *ui_page_template_env_resolve_family(void)
{
    if (g_ui_template_env_subset != 0U)
    {
        return (ui_get_track_type(ui_get_active_lane()) == UI_TRACK_TYPE_GROUP)
                ? &g_ui_template_env_family_group_retrig
                : &g_ui_template_env_family_retrig;
    }

    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_ENV);
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

static ui_template_custom_widget_kind_t ui_page_template_env_pick_custom_widget(uint8_t slot,
                                                                                   const ui_template_subpage_t *subpage,
                                                                                   param_id_t id)
{
    if (id == PARAM_FILTER_MODE)
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_FILTER_TYPE;
    }

    if ((subpage != 0)
            && (subpage->param_bank.params[3] == PARAM_FILTER_MORPH)
            && (slot == 0U)
            && (id == PARAM_FILTER_CUTOFF))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP;
    }

    if ((subpage != 0)
            && (subpage->param_bank.params[3] == PARAM_FILTER_MORPH)
            && (slot == 1U)
            && (id == PARAM_FILTER_RESONANCE))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP;
    }

    if ((subpage != 0) && (subpage->param_bank.params[3] == PARAM_FILTER_MORPH)
            && (slot == 3U) && (id == PARAM_FILTER_MORPH))
        return UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP;

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

static ui_template_page_state_t g_ui_template_env_state = {
    .family = 0,
    .family_resolver = ui_page_template_env_resolve_family,
    .custom_widget_picker = ui_page_template_env_pick_custom_widget,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_env_open_primary(void)
{
    g_ui_template_env_subset = 0U;
    g_ui_template_env_state.navigation_subset = 0U;
    ui_template_page_select_subpage(&g_ui_template_env_state, 0U);
}

uint8_t ui_page_template_env_open_vca(void)
{
    g_ui_template_env_subset = 0U;
    g_ui_template_env_state.navigation_subset = 0U;
    ui_template_page_select_subpage(&g_ui_template_env_state, 2U);
    return (g_ui_template_env_state.active_subpage == 2U) ? 1U : 0U;
}

void ui_page_template_env_toggle_subset(void)
{
    g_ui_template_env_subset = (g_ui_template_env_subset == 0U) ? 1U : 0U;
    g_ui_template_env_state.navigation_subset = g_ui_template_env_subset;
    ui_navigation_restore_current_template_subpage();
}

typedef struct
{
    uint8_t valid;
    uint8_t active_track;
    uint8_t family;
    uint8_t type;
    uint8_t filter_morph_ui;
    uint32_t runtime_track_revision;
} ui_page_template_env_sync_cache_t;

static ui_page_template_env_sync_cache_t g_ui_template_env_sync_cache = { 0U };

void ui_page_template_env_register_families(void)
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

            ui_template_family_register(UI_TEMPLATE_FAMILY_ENV,
                                        track_family,
                                        track_type,
                                        (track_type == UI_TRACK_TYPE_GROUP)
                                                ? &g_ui_template_env_family_group
                                                : &g_ui_template_env_family_audio);
        }
    }
}

static ui_template_family_t *ui_page_template_env_get_audio_family(void)
{
    return (ui_template_family_t *)ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_ENV);
}

static uint8_t ui_page_template_env_sync_should_recompute(uint8_t active_track,
                                                             ui_track_family_t active_family,
                                                             ui_track_type_t active_type)
{
    /* Consumer-edge refresh: revision checks use a refreshed projection; the revision is a coherence guard only. */
    const uint32_t runtime_track_revision = track_runtime_get_track_revision(active_track);
    const uint8_t filter_morph_ui = (uint8_t)(param_store_get_active(PARAM_FILTER_MORPH) + 0.5f);

    if ((g_ui_template_env_sync_cache.valid != 0U)
            && (g_ui_template_env_sync_cache.active_track == active_track)
            && (g_ui_template_env_sync_cache.family == (uint8_t)active_family)
            && (g_ui_template_env_sync_cache.type == (uint8_t)active_type)
            && (g_ui_template_env_sync_cache.filter_morph_ui == filter_morph_ui)
            && (g_ui_template_env_sync_cache.runtime_track_revision == runtime_track_revision))
    {
        return 0U;
    }

    g_ui_template_env_sync_cache.valid = 1U;
    g_ui_template_env_sync_cache.active_track = active_track;
    g_ui_template_env_sync_cache.family = (uint8_t)active_family;
    g_ui_template_env_sync_cache.type = (uint8_t)active_type;
    g_ui_template_env_sync_cache.filter_morph_ui = filter_morph_ui;
    g_ui_template_env_sync_cache.runtime_track_revision = runtime_track_revision;
    return 1U;
}

static void ui_page_template_env_sync_family(void)
{
    if (g_ui_template_env_subset != 0U)
    {
        return;
    }

    ui_template_family_t *family = ui_page_template_env_get_audio_family();
    uint8_t filter_target_track = 0U;
    const uint8_t active_track = ui_get_active_lane();
    const ui_track_family_t active_family = ui_get_track_family(active_track);
    const ui_track_type_t active_type = ui_get_track_type(active_track);
    if (family == 0)
    {
        return;
    }

    if (ui_page_template_env_sync_should_recompute(active_track, active_family, active_type) == 0U)
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

    const uint8_t has_vca_page = track_runtime_supports_vca_gate(track_runtime_get_ctx(active_track));

    family->nav_labels[0] = "FILTER";
    family->nav_labels[1] = "ADSR";
    family->nav_labels[2] = (has_vca_page != 0U) ? "VCA" : "-";
    family->nav_labels[3] = "ENV 3";

    family->subpages[0].title = "FILTER";
    family->subpages[0].param_bank.params[0] = PARAM_FILTER_CUTOFF;
    family->subpages[0].param_bank.params[1] = PARAM_FILTER_RESONANCE;
    family->subpages[0].param_bank.params[2] = PARAM_FILTER_EG_AMT;
    family->subpages[0].param_bank.params[3] = PARAM_FILTER_MORPH;

    family->subpages[1].title = "ADSR";
    family->subpages[1].param_bank.params[0] = PARAM_FILTER_ATTACK;
    family->subpages[1].param_bank.params[1] = PARAM_FILTER_DECAY;
    family->subpages[1].param_bank.params[2] = PARAM_FILTER_SUSTAIN;
    family->subpages[1].param_bank.params[3] = PARAM_FILTER_RELEASE;

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

static void ui_page_template_env_enter(void)
{
    ui_page_template_env_sync_family();
    ui_template_page_enter();
}

static void ui_page_template_env_handle_event(const ui_event_t *ev)
{
    ui_template_page_handle_event(ev);
    ui_page_template_env_sync_family();
    ui_template_page_select_subpage(&g_ui_template_env_state, g_ui_template_env_state.active_subpage);
}

static void ui_page_template_env_tick(void)
{
    ui_page_template_env_sync_family();
    ui_template_page_tick();
}

static void ui_page_template_env_sync_active_context(void)
{
    ui_page_template_env_sync_family();
    ui_template_page_sync_active_track_context();
}

static void ui_page_template_env_render(void)
{
    ui_page_template_env_sync_family();
    ui_template_page_render();
}

const ui_page_t g_ui_page_template_env = {
    .enter = ui_page_template_env_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_page_template_env_handle_event,
    .tick = ui_page_template_env_tick,
    .sync_active_context = ui_page_template_env_sync_active_context,
    .render = ui_page_template_env_render,
    .context = &g_ui_template_env_state,
};
