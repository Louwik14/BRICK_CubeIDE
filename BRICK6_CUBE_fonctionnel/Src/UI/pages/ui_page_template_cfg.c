#include "pages/ui_page_template_cfg.h"

#include <string.h>

#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_cfg_family = {
    .family_title = "CFG",
    .nav_labels = { "MAIN", "-", "-", "-" },
    .subpages = {
        {
            .title = "TRACK",
            .param_bank = { .params = { PARAM_CFG_TRACK, PARAM_CFG_TRACK_TYPE, PARAM_CFG_MIDI_CH, PARAM_CFG_MIDI_SRC } },
        },
        {
            .title = "REC",
            .param_bank = { .params = { PARAM_CFG_REC, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
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

static uiw_widget_type_t ui_page_template_cfg_pick_widget(uint8_t slot,
                                                          param_id_t id,
                                                          const char *value_label,
                                                          uiw_widget_type_t suggested_widget)
{
    (void)slot;

    if (id == PARAM_CFG_TRACK_TYPE)
    {
        return UIW_WIDGET_EMPTY;
    }

    if (id != PARAM_CFG_TRACK)
    {
        return suggested_widget;
    }

    if ((value_label != NULL) && (strncmp(value_label, "Off", 3) == 0))
    {
        return UIW_WIDGET_EMPTY;
    }

    if ((value_label != NULL) && (strncmp(value_label, "Synth", 5) == 0))
    {
        return UIW_WIDGET_KEYBOARD;
    }

    return UIW_WIDGET_JACK;
}

static const ui_template_family_t *ui_page_template_cfg_resolve_family(void)
{
    if (ui_get_track_family(ui_get_active_track()) == UI_TRACK_FAMILY_OFF)
    {
        return &g_ui_template_cfg_family;
    }

    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_CFG);
}

static ui_template_page_state_t g_ui_template_cfg_state = {
    .family = 0,
    .family_resolver = ui_page_template_cfg_resolve_family,
    .widget_picker = ui_page_template_cfg_pick_widget,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_cfg_register_families(void)
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

            ui_template_family_register(UI_TEMPLATE_FAMILY_CFG, track_family, track_type, &g_ui_template_cfg_family);
        }
    }
}

const ui_page_t g_ui_page_template_cfg = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .render = ui_template_page_render,
    .context = &g_ui_template_cfg_state,
};

void ui_page_template_cfg_open_rec(void)
{
    ui_template_page_select_subpage(&g_ui_template_cfg_state, 1U);
}
