#include "pages/ui_page_template_mod.h"

#include <stdio.h>

#include "Mod/mod_lfo_v1.h"
#include "ui_core.h"
#include "ui_navigation.h"
#include "ui_template_page.h"

static uint8_t g_ui_template_mod_subset = 0U;

static const ui_template_family_t g_ui_template_mod_family_main = {
    .family_title = "MOD 1/2",
    .nav_labels = { "MATRIX", "LFO 1", "LFO 2", "LFO 3" },
    .subpages = {
        {
            .title = "MATRIX",
            .param_bank = { .params = { PARAM_MOD_MATRIX_SLOT, PARAM_MOD_MATRIX_SOURCE, PARAM_MOD_MATRIX_DEST, PARAM_MOD_MATRIX_DEPTH } },
        },
        {
            .title = "LFO 1",
            .param_bank = { .params = { PARAM_LFO1_RATE, PARAM_LFO1_SHAPE, PARAM_LFO1_PHASE, PARAM_LFO1_TRIG } },
        },
        {
            .title = "LFO 2",
            .param_bank = { .params = { PARAM_LFO2_RATE, PARAM_LFO2_SHAPE, PARAM_LFO2_PHASE, PARAM_LFO2_TRIG } },
        },
        {
            .title = "LFO 3",
            .param_bank = { .params = { PARAM_LFO3_RATE, PARAM_LFO3_SHAPE, PARAM_LFO3_PHASE, PARAM_LFO3_TRIG } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_mod_family_ops = {
    .family_title = "MOD 2/2",
    .nav_labels = { "MULTI", "SLEW", "-", "-" },
    .subpages = {
        {
            .title = "MULTI",
            .param_bank = { .params = { PARAM_MOD_MULTI_1_A, PARAM_MOD_MULTI_1_B, PARAM_MOD_MULTI_2_A, PARAM_MOD_MULTI_2_B } },
        },
        {
            .title = "SLEW",
            .param_bank = { .params = { PARAM_MOD_SLEW_1_SOURCE, PARAM_MOD_SLEW_1_AMOUNT, PARAM_MOD_SLEW_2_SOURCE, PARAM_MOD_SLEW_2_AMOUNT } },
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

static const ui_template_family_t *ui_page_template_mod_resolve_family(void)
{
    if (g_ui_template_mod_subset != 0U)
    {
        return &g_ui_template_mod_family_ops;
    }

    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_MOD);
}

static uint8_t ui_page_template_mod_is_lfo_param(param_id_t id, mod_lfo_param_t param)
{
    static const param_id_t lfo_params[MOD_LFO_COUNT_PER_TRACK][MOD_LFO_PARAM_COUNT] = {
        { PARAM_LFO1_RATE, PARAM_LFO1_SHAPE, PARAM_LFO1_TRIG, PARAM_LFO1_PHASE },
        { PARAM_LFO2_RATE, PARAM_LFO2_SHAPE, PARAM_LFO2_TRIG, PARAM_LFO2_PHASE },
        { PARAM_LFO3_RATE, PARAM_LFO3_SHAPE, PARAM_LFO3_TRIG, PARAM_LFO3_PHASE },
    };

    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        if (lfo_params[lfo][param] == id)
        {
            return 1U;
        }
    }

    return 0U;
}

static ui_template_custom_widget_kind_t ui_page_template_mod_pick_custom_widget(uint8_t slot,
                                                                                const ui_template_subpage_t *subpage,
                                                                                param_id_t id)
{
    (void)subpage;

    if ((slot == 0U) && (id == PARAM_MOD_MATRIX_SLOT))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SLOT;
    }
    if ((slot == 1U) && (id == PARAM_MOD_MATRIX_SOURCE))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SOURCE;
    }
    if (((id == PARAM_MOD_MULTI_1_A)
            || (id == PARAM_MOD_MULTI_1_B)
            || (id == PARAM_MOD_MULTI_2_A)
            || (id == PARAM_MOD_MULTI_2_B)
            || (id == PARAM_MOD_SLEW_1_SOURCE)
            || (id == PARAM_MOD_SLEW_2_SOURCE)))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_MATRIX_SOURCE;
    }
    if ((slot == 2U) && (id == PARAM_MOD_MATRIX_DEST))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEST;
    }
    if ((slot == 0U) && (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_RATE) != 0U))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_LFO_RATE;
    }
    if ((slot == 3U) && (id == PARAM_MOD_MATRIX_DEPTH))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEPTH;
    }
    if ((id == PARAM_MOD_SLEW_1_AMOUNT) || (id == PARAM_MOD_SLEW_2_AMOUNT))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEPTH;
    }
    if (((slot == 1U) && (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_SHAPE) != 0U))
            || ((slot == 2U) && (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_PHASE) != 0U)))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_LFO_SHAPE_PHASE_GROUP;
    }
    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static uiw_widget_type_t ui_page_template_mod_pick_widget(uint8_t slot,
                                                          param_id_t id,
                                                          const char *value_label,
                                                          uiw_widget_type_t suggested_widget)
{
    (void)slot;
    (void)value_label;

    if (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_TRIG) != 0U)
    {
        return UIW_WIDGET_ENUM_TEXT;
    }

    return suggested_widget;
}

static uint8_t ui_page_template_mod_param_text(uint8_t slot,
                                               param_id_t id,
                                               float value,
                                               char *out_name,
                                               uint32_t out_name_len,
                                               char *out_value,
                                               uint32_t out_value_len)
{
    (void)slot;
    (void)value;
    if ((out_name == NULL) || (out_name_len == 0U))
    {
        return 0U;
    }

    if (ui_page_template_mod_is_lfo_param(id, MOD_LFO_PARAM_PHASE) != 0U)
    {
        uint8_t lfo_index = MOD_LFO_COUNT_PER_TRACK;
        for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
        {
            if (g_ui_template_mod_family_main.subpages[lfo + 1U].param_bank.params[2] == id)
            {
                lfo_index = lfo;
                break;
            }
        }
        const uint8_t is_rnd = ((lfo_index < MOD_LFO_COUNT_PER_TRACK)
                && (mod_lfo_v1_shape_is_random(ui_get_active_lane(), lfo_index) != 0U)) ? 1U : 0U;
        (void)snprintf(out_name, out_name_len, "%s", (is_rnd != 0U) ? "Slew" : "Phase");
        if ((out_value != NULL) && (out_value_len > 0U) && (is_rnd != 0U))
        {
            (void)snprintf(out_value, out_value_len, "%u%%", (unsigned int)((value * 100.0f / 360.0f) + 0.5f));
        }
        return 1U;
    }

    return 1U;
}

static ui_template_page_state_t g_ui_template_mod_state = {
    .family = 0,
    .family_resolver = ui_page_template_mod_resolve_family,
    .widget_picker = ui_page_template_mod_pick_widget,
    .custom_widget_picker = ui_page_template_mod_pick_custom_widget,
    .param_text = ui_page_template_mod_param_text,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_mod_open_primary(void)
{
    g_ui_template_mod_subset = 0U;
    g_ui_template_mod_state.navigation_subset = 0U;
    ui_template_page_select_subpage(&g_ui_template_mod_state, 0U);
}

void ui_page_template_mod_toggle_subset(void)
{
    g_ui_template_mod_subset = (g_ui_template_mod_subset == 0U) ? 1U : 0U;
    g_ui_template_mod_state.navigation_subset = g_ui_template_mod_subset;
    ui_navigation_restore_current_template_subpage();
}

void ui_page_template_mod_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; ++family)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        for (uint8_t type = 0U; type < (uint8_t)UI_TRACK_TYPE_COUNT; ++type)
        {
            const ui_track_type_t track_type = (ui_track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type))
            {
                continue;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_MOD,
                                        track_family,
                                        track_type,
                                        &g_ui_template_mod_family_main);
        }
    }
}

const ui_page_t g_ui_page_template_mod = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_mod_state,
};
