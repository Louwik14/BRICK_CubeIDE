#include "pages/ui_page_template_cfg.h"

#include <stdio.h>
#include <string.h>

#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_cfg_family = {
    .family_title = "CFG",
    .nav_labels = { "MAIN", "MIDI", "-", "-" },
    .subpages = {
        {
            .title = "TRACK",
            .param_bank = { .params = { PARAM_CFG_TRACK, PARAM_CFG_TRACK_TYPE, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "MIDI",
            .param_bank = { .params = { PARAM_CFG_MIDI_CH, PARAM_CFG_MIDI_SRC, PARAM_COUNT, PARAM_COUNT } },
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

static const ui_template_family_t g_ui_template_cfg_synth_family = {
    .family_title = "CFG",
    .nav_labels = { "MAIN", "MIDI", "-", "-" },
    .subpages = {
        { .title = "TRACK", .param_bank = { .params = {
            PARAM_CFG_TRACK, PARAM_CFG_TRACK_TYPE, PARAM_CFG_POLY_VOICES, PARAM_CFG_POLY_SPREAD } } },
        { .title = "MIDI", .param_bank = { .params = {
            PARAM_CFG_MIDI_CH, PARAM_CFG_MIDI_SRC, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = {
            PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_cfg_slave_proxy_family = {
    .family_title = "CFG",
    .nav_labels = { "MAIN", "-", "-", "-" },
    .subpages = {
        {
            .title = "TRACK",
            .param_bank = { .params = { PARAM_CFG_TRACK, PARAM_CFG_TRACK_TYPE, PARAM_COUNT, PARAM_COUNT } },
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

static const ui_template_family_t g_ui_template_cfg_group_master_family = {
    .family_title = "CFG",
    .nav_labels = { "MAIN", "MIDI", "GROUP", "-" },
    .subpages = {
        {
            .title = "TRACK",
            .param_bank = { .params = { PARAM_CFG_TRACK, PARAM_CFG_TRACK_TYPE, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "MIDI",
            .param_bank = { .params = { PARAM_CFG_MIDI_CH, PARAM_CFG_MIDI_SRC, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "GROUP",
            .param_bank = { .params = { PARAM_CFG_GROUP_SPREAD, PARAM_CFG_GROUP_SPREAD_KEYTRK, PARAM_CFG_GROUP_LINK, PARAM_CFG_GROUP_SEQ_LINK } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_rec_cfg_family = {
    .family_title = "REC CFG",
    .nav_labels = { "MAIN", "LEN", "-", "-" },
    .subpages = {
        {
            .title = "MAIN",
            .param_bank = { .params = { PARAM_CFG_START, PARAM_CFG_TEMPO, PARAM_CFG_SYNC, PARAM_CFG_METRO } },
        },
        {
            .title = "LEN",
            .param_bank = { .params = { PARAM_CFG_REC_LEN, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
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

    if ((value_label != NULL) && ((strncmp(value_label, "Synth", 5) == 0)
            || (strncmp(value_label, "Sampler", 7) == 0)))
    {
        return UIW_WIDGET_KEYBOARD;
    }

    return UIW_WIDGET_JACK;
}

static uiw_widget_type_t ui_page_template_rec_cfg_pick_widget(uint8_t slot,
                                                              param_id_t id,
                                                              const char *value_label,
                                                              uiw_widget_type_t suggested_widget)
{
    (void)slot;
    (void)value_label;
    if (id == PARAM_CFG_TEMPO)
    {
        return UIW_WIDGET_ENUM_TEXT;
    }
    return suggested_widget;
}

static ui_template_custom_widget_kind_t ui_page_template_cfg_pick_custom_widget(uint8_t slot,
                                                                                const ui_template_subpage_t *subpage,
                                                                                param_id_t id)
{
    if ((subpage != NULL)
            && (subpage->param_bank.params[0] == PARAM_CFG_GROUP_SPREAD)
            && (subpage->param_bank.params[1] == PARAM_CFG_GROUP_SPREAD_KEYTRK)
            && (slot < 2U)
            && ((id == PARAM_CFG_GROUP_SPREAD) || (id == PARAM_CFG_GROUP_SPREAD_KEYTRK)))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_CFG_SPREAD_KEYTRACK_GROUP;
    }

    if ((subpage == NULL)
            || (subpage->param_bank.params[0] != PARAM_CFG_TRACK)
            || (ui_get_track_family(ui_get_active_track()) == UI_TRACK_FAMILY_OFF))
    {
        if ((subpage != NULL) && (subpage->param_bank.params[0] == PARAM_CFG_TRACK) && (slot == 0U) && (id == PARAM_CFG_TRACK))
        {
            return UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TRACK;
        }
        if ((subpage != NULL) && (subpage->param_bank.params[0] == PARAM_CFG_TRACK) && (slot < 4U))
        {
            return UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_INACTIVE;
        }
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    switch (id)
    {
        case PARAM_CFG_TRACK:
            return UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TRACK;

        case PARAM_CFG_TRACK_TYPE:
            return UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TYPE;

        case PARAM_CFG_MIDI_CH:
            return UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_CHANNEL;

        case PARAM_CFG_MIDI_SRC:
            return UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_SOURCE;

        default:
            return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }
}

static uint8_t ui_page_template_cfg_param_text(uint8_t slot,
                                               param_id_t id,
                                               float value,
                                               char *out_name,
                                               uint32_t out_name_len,
                                               char *out_value,
                                               uint32_t out_value_len)
{
    (void)value;
    (void)out_value;
    (void)out_value_len;

    if ((slot == 0U) && (id == PARAM_CFG_GROUP_SPREAD) && (out_name != NULL) && (out_name_len > 0U))
    {
        (void)snprintf(out_name, out_name_len, "AMT");
        return 1U;
    }
    if ((slot == 1U) && (id == PARAM_CFG_GROUP_SPREAD_KEYTRK) && (out_name != NULL) && (out_name_len > 0U))
    {
        (void)snprintf(out_name, out_name_len, "KEY");
        return 1U;
    }
    return 0U;
}

static const ui_template_family_t *ui_page_template_cfg_resolve_family(void)
{
    const uint8_t active_track = ui_get_active_track();
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(active_track, &role_u8);
    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        return &g_ui_template_cfg_slave_proxy_family;
    }

    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        uint8_t member_count = 0U;
        if ((track_runtime_collect_voice_group_members(active_track, NULL, 0U, &member_count) != 0U)
                && (member_count > 1U)
                && (member_count <= 8U))
        {
            return &g_ui_template_cfg_group_master_family;
        }
    }

    if (ui_get_track_family(active_track) == UI_TRACK_FAMILY_OFF)
    {
        return &g_ui_template_cfg_family;
    }

    if (ui_get_track_family(active_track) == UI_TRACK_FAMILY_SYNTH)
    {
        return &g_ui_template_cfg_synth_family;
    }

    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_CFG);
}

static uint8_t ui_page_template_cfg_virtual_slot_text(uint8_t slot,
                                                       char *out_name,
                                                       uint32_t out_name_len,
                                                       char *out_value,
                                                       uint32_t out_value_len)
{
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    const uint8_t active_track = ui_get_active_track();
    (void)track_runtime_get_voice_group_role(active_track, &role_u8);
    if (role_u8 != (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        return 0U;
    }

    if ((slot != 2U) && (slot != 3U))
    {
        return 0U;
    }

    uint8_t master_track = active_track;
    if (track_runtime_get_voice_group_effective_master(active_track, &master_track) == 0U)
    {
        return 0U;
    }

    if ((out_name != NULL) && (out_name_len > 0U))
    {
        (void)snprintf(out_name, out_name_len, "%s", (slot == 2U) ? "Midi CH" : "Midi Src");
    }
    if ((out_value != NULL) && (out_value_len > 0U))
    {
        (void)snprintf(out_value, out_value_len, "M%u", (unsigned int)(master_track + 1U));
    }
    return 1U;
}

static ui_template_page_state_t g_ui_template_cfg_state = {
    .family = 0,
    .family_resolver = ui_page_template_cfg_resolve_family,
    .widget_picker = ui_page_template_cfg_pick_widget,
    .custom_widget_picker = ui_page_template_cfg_pick_custom_widget,
    .virtual_slot_text = ui_page_template_cfg_virtual_slot_text,
    .param_text = ui_page_template_cfg_param_text,
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
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_cfg_state,
};

static ui_template_page_state_t g_ui_template_rec_cfg_state = {
    .family = &g_ui_template_rec_cfg_family,
    .family_resolver = 0,
    .widget_picker = ui_page_template_rec_cfg_pick_widget,
    .active_subpage = 0U,
    .has_visited = 0U,
};

const ui_page_t g_ui_page_template_rec_cfg = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_rec_cfg_state,
};

void ui_page_template_rec_cfg_open_main(void)
{
    ui_template_page_select_subpage(&g_ui_template_rec_cfg_state, 0U);
}
