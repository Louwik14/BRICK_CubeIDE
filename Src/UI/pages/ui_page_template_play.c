#include "pages/ui_page_template_play.h"

#include <stdio.h>

#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_play_family = {
    .family_title = "PLAY",
    .nav_labels = { "V1", "V2", "V3", "V4" },
    .subpages = {
        { .title = "Voice 1", .param_bank = { .params = { PARAM_SEQ_PLAY_V1_NOTE, PARAM_SEQ_PLAY_V1_VEL, PARAM_SEQ_PLAY_V1_LEN, PARAM_SEQ_PLAY_V1_MICTIM } } },
        { .title = "Voice 2", .param_bank = { .params = { PARAM_SEQ_PLAY_V2_NOTE, PARAM_SEQ_PLAY_V2_VEL, PARAM_SEQ_PLAY_V2_LEN, PARAM_SEQ_PLAY_V2_MICTIM } } },
        { .title = "Voice 3", .param_bank = { .params = { PARAM_SEQ_PLAY_V3_NOTE, PARAM_SEQ_PLAY_V3_VEL, PARAM_SEQ_PLAY_V3_LEN, PARAM_SEQ_PLAY_V3_MICTIM } } },
        { .title = "Voice 4", .param_bank = { .params = { PARAM_SEQ_PLAY_V4_NOTE, PARAM_SEQ_PLAY_V4_VEL, PARAM_SEQ_PLAY_V4_LEN, PARAM_SEQ_PLAY_V4_MICTIM } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t g_ui_template_play_slave_proxy_family = {
    .family_title = "PLAY",
    .nav_labels = { "V1", "V2", "V3", "V4" },
    .subpages = {
        { .title = "Voice 1", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "Voice 2", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "Voice 3", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "Voice 4", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t *ui_page_template_play_resolve_family(void)
{
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(ui_get_active_track(), &role_u8);
    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        return &g_ui_template_play_slave_proxy_family;
    }

    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_PLAY);
}

static uint8_t ui_page_template_play_subpage_enabled(uint8_t subpage_index)
{
    const uint8_t active_track = ui_get_active_track();
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(active_track, &role_u8);
    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        return (subpage_index == 0U) ? 1U : 0U;
    }

    /* Consumer-edge refresh: subpage availability is a pure projection read after refresh. */
    track_runtime_refresh_track(active_track);
    track_runtime_resolved_track_t resolved;
    if (track_runtime_resolve_track(active_track, &resolved) == 0U)
    {
        return 0U;
    }

    return (subpage_index < track_runtime_get_play_voice_count_from_descriptor(&resolved.descriptor)) ? 1U : 0U;
}

static uint8_t ui_page_template_play_virtual_slot_text(uint8_t slot,
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

    uint8_t master_track = active_track;
    if (track_runtime_get_voice_group_effective_master(active_track, &master_track) == 0U)
    {
        return 0U;
    }

    if ((out_name != NULL) && (out_name_len > 0U))
    {
        if (slot == 0U)
        {
            (void)snprintf(out_name, out_name_len, "Notes");
        }
        else if (slot == 1U)
        {
            (void)snprintf(out_name, out_name_len, "Params");
        }
        else
        {
            (void)snprintf(out_name, out_name_len, "-");
        }
    }

    if ((out_value != NULL) && (out_value_len > 0U))
    {
        if (slot == 0U)
        {
            (void)snprintf(out_value, out_value_len, "M%u", (unsigned int)(master_track + 1U));
        }
        else if (slot == 1U)
        {
            (void)snprintf(out_value, out_value_len, "S%u", (unsigned int)(active_track + 1U));
        }
        else
        {
            (void)snprintf(out_value, out_value_len, "-");
        }
    }
    return 1U;
}

static ui_template_page_state_t g_ui_template_play_state = {
    .family = 0,
    .family_resolver = ui_page_template_play_resolve_family,
    .subpage_enabled = ui_page_template_play_subpage_enabled,
    .virtual_slot_text = ui_page_template_play_virtual_slot_text,
    .active_subpage = 0U,
    .has_visited = 0U,
};

void ui_page_template_play_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; ++family)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        if ((ui_track_family_is_engine(track_family) == 0U)
                && (track_family != UI_TRACK_FAMILY_MIDI)
                && (ui_track_family_is_input(track_family) == 0U))
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
            if ((ui_track_family_is_input(track_family) != 0U)
                    && (track_type != UI_TRACK_TYPE_HYBRID))
            {
                continue;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_PLAY,
                                        track_family,
                                        track_type,
                                        &g_ui_template_play_family);
        }
    }
}

const ui_page_t g_ui_page_template_play = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_play_state,
};
