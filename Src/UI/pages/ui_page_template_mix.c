#include "pages/ui_page_template_mix.h"

#include "Core/track_runtime.h"
#include "ui_core.h"
#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_mix_family = {
    .family_title = "MIX",
    .nav_labels = { "MIX", "REV1", "REV2", "-" },
    .subpages = {
        {
            .title = "MIX",
            .param_bank = { .params = { PARAM_MIX_LEVEL, PARAM_MIX_PAN, PARAM_MIX_SEND1, PARAM_MIX_SEND2 } },
        },
        {
            .title = "REVB",
            .param_bank = { .params = { PARAM_MIX_REVERB_WET, PARAM_MIX_REVERB_SIZE, PARAM_MIX_REVERB_DECAY, PARAM_MIX_REVERB_PRED } },
        },
        {
            .title = "REVB",
            .param_bank = { .params = { PARAM_MIX_REVERB_TYPE, PARAM_MIX_REVERB_SURR, PARAM_COUNT, PARAM_COUNT } },
        },
        {
            .title = "-",
            .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } },
        },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_mix_unavailable_family = {
    .family_title = "MIX",
    .nav_labels = { "MIX", "-", "-", "-" },
    .subpages = {
        {
            .title = "N/A",
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

static const ui_template_family_t *ui_page_template_mix_resolve_family(void)
{
    track_runtime_refresh_track(ui_get_active_track());
    if (track_runtime_is_audio_routable(ui_get_active_track()) == 0U)
    {
        return &g_ui_template_mix_unavailable_family;
    }

    return &g_ui_template_mix_family;
}

static ui_template_page_state_t g_ui_template_mix_state = {
    .family = 0,
    .family_resolver = ui_page_template_mix_resolve_family,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_mix_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; ++family)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        if (!ui_track_family_is_input(track_family)
                && (ui_track_family_is_engine(track_family) == 0)
                && (track_family != UI_TRACK_FAMILY_MASTER))
        {
            continue;
        }

        for (uint8_t type = 0U; type < (uint8_t)UI_TRACK_TYPE_COUNT; ++type)
        {
            const ui_track_type_t track_type = (ui_track_type_t)type;
            if (!ui_track_type_is_valid_for_family(track_family, track_type))
            {
                continue;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_MIX,
                                        track_family,
                                        track_type,
                                        &g_ui_template_mix_family);
        }
    }
}

const ui_page_t g_ui_page_template_mix = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .render = ui_template_page_render,
    .context = &g_ui_template_mix_state,
};
