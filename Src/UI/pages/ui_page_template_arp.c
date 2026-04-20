#include "pages/ui_page_template_arp.h"

#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_arp_family = {
    .family_title = "ARP",
    .nav_labels = { "CORE", "GROOVE", "STRUM", "PITCH" },
    .subpages = {
        {
            .title = "CORE",
            .param_bank = { .params = { PARAM_ARP_HOLD, PARAM_ARP_RATE, PARAM_ARP_OCT, PARAM_ARP_PATTERN } },
        },
        {
            .title = "GROOVE",
            .param_bank = { .params = { PARAM_ARP_GATE, PARAM_ARP_SWING, PARAM_ARP_ACCENT, PARAM_ARP_VEL_ACC } },
        },
        {
            .title = "STRUM",
            .param_bank = { .params = { PARAM_ARP_STRUM, PARAM_ARP_OFFSET, PARAM_COUNT, PARAM_ARP_TRANS } },
        },
        {
            .title = "PITCH",
            .param_bank = { .params = { PARAM_ARP_SPREAD, PARAM_ARP_DIR, PARAM_ARP_SYNC, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_arp_family_buffer = {
    .family_title = "ROUT",
    .nav_labels = { "ROUT", "-", "-", "-" },
    .subpages = {
        {
            .title = "ROUT",
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

static const ui_template_family_t *ui_page_template_arp_resolve_family(void)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_hall_mode_effective_view_t effective_view =
        ui_hall_mode_resolve_effective_view(active_track, UI_HALL_MODE_ARP);

    if (effective_view == UI_HALL_MODE_VIEW_ROUT)
    {
        return &g_ui_template_arp_family_buffer;
    }

    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_ARP);
}

static ui_template_page_state_t g_ui_template_arp_state = {
    .family = 0,
    .family_resolver = ui_page_template_arp_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

static void ui_page_template_arp_handle_event(const ui_event_t *ev)
{
    ui_template_page_handle_event(ev);
}

void ui_page_template_arp_register_families(void)
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

            ui_template_family_register(UI_TEMPLATE_FAMILY_ARP,
                                        track_family,
                                        track_type,
                                        &g_ui_template_arp_family);
        }
    }
}

const ui_page_t g_ui_page_template_arp = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_page_template_arp_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_arp_state,
};
