#include "pages/ui_page_template_macro.h"

#include <stdio.h>

#include "ui_template_page.h"
#include "ui_macro_interaction.h"
#include "Storage/project_v1.h"

static const ui_template_family_t g_ui_template_macro_family = {
    .family_title = "MACRO CFG",
    .nav_labels = { "MAIN", "-", "-", "-" },
    .subpages = {
        {
            .title = "MODE",
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
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static uint8_t ui_page_template_macro_virtual_slot_text(uint8_t slot,
                                                        char *out_name,
                                                        uint32_t out_name_len,
                                                        char *out_value,
                                                        uint32_t out_value_len)
{
    if ((out_name == NULL) || (out_value == NULL) || (out_name_len == 0U) || (out_value_len == 0U))
    {
        return 0U;
    }

    if (slot != 0U)
    {
        return 0U;
    }

    const char *const value =
        (project_v1_macro_get_hall_switch_mode() == PROJECT_V1_MACRO_HALL_SWITCH_BANK) ? "Bank" : "Slot";
    (void)snprintf(out_name, out_name_len, "Hall Switch Mode");
    (void)snprintf(out_value, out_value_len, "%s", value);
    return 1U;
}

static ui_template_page_state_t g_ui_template_macro_state = {
    .family = &g_ui_template_macro_family,
    .family_resolver = 0,
    .widget_picker = 0,
    .subpage_enabled = 0,
    .virtual_slot_text = ui_page_template_macro_virtual_slot_text,
    .resolved_family = 0,
    .active_subpage = 0U,
    .has_visited = 0U,
};

static void ui_page_template_macro_enter(void)
{
    ui_macro_interaction_reset();
    ui_template_page_enter();
}

void ui_page_template_macro_handle_encoder(uint8_t encoder, int16_t delta)
{
    if (encoder != 0U)
    {
        return;
    }

    if (delta == 0)
    {
        return;
    }

    if (delta > 0)
    {
        project_v1_macro_set_hall_switch_mode(PROJECT_V1_MACRO_HALL_SWITCH_BANK);
        ui_macro_interaction_reset();
        return;
    }

    project_v1_macro_set_hall_switch_mode(PROJECT_V1_MACRO_HALL_SWITCH_SLOT);
    ui_macro_interaction_reset();
}

const ui_page_t g_ui_page_template_macro = {
    .enter = ui_page_template_macro_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_macro_state,
};
