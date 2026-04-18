#include "pages/ui_page_template_keyboard.h"

#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_keyboard_family = {
    .family_title = "KEYBOARD",
    .nav_labels = { "PLAY", "MODE", "-", "-" },
    .subpages = {
        {
            .title = "PLAY",
            .param_bank = { .params = { PARAM_KBD_ROOT, PARAM_KBD_SCALE, PARAM_KBD_OMNICHORD, PARAM_COUNT } },
        },
        {
            .title = "MODE",
            .param_bank = { .params = { PARAM_KBD_NOTE_ORDER, PARAM_KBD_CHORD_OVERRIDE, PARAM_COUNT, PARAM_COUNT } },
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

static const ui_template_family_t *ui_page_template_keyboard_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_KEYBOARD);
}

static ui_template_page_state_t g_ui_template_keyboard_state = {
    .family = 0,
    .family_resolver = ui_page_template_keyboard_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

static void ui_page_template_keyboard_handle_event(const ui_event_t *ev)
{
    ui_template_page_handle_event(ev);
}

void ui_page_template_keyboard_register_families(void)
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

            ui_template_family_register(UI_TEMPLATE_FAMILY_KEYBOARD,
                                        track_family,
                                        track_type,
                                        &g_ui_template_keyboard_family);
        }
    }
}

const ui_page_t g_ui_page_template_keyboard = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_page_template_keyboard_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_keyboard_state,
};
