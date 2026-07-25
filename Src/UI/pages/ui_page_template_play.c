#include "pages/ui_page_template_play.h"

#include <stdio.h>

#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"

static uint8_t g_ui_template_play_subset = 0U;
static ui_template_family_t g_ui_template_play_group_family;

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

static uint8_t ui_page_template_play_is_play_param(param_id_t param)
{
    return (uint8_t)((param >= PARAM_SEQ_PLAY_V1_NOTE) && (param <= PARAM_SEQ_PLAY_V4_MICTIM));
}

static uint8_t ui_page_template_play_collect_active_group(uint8_t *out_members,
                                                          uint8_t out_capacity,
                                                          uint8_t *out_count)
{
    if (out_count == NULL)
    {
        return 0U;
    }

    *out_count = 0U;
    const uint8_t active_track = ui_get_active_track();
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(active_track, &role_u8);
    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        return 0U;
    }
    if (role_u8 != (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        if ((out_members != NULL) && (out_capacity > 0U))
        {
            out_members[0] = active_track;
        }
        *out_count = 1U;
        return 1U;
    }

    uint8_t members[SEQ_TRACK_COUNT];
    uint8_t member_count = 0U;
    if ((track_runtime_collect_voice_group_members(active_track,
                                                   members,
                                                   (uint8_t)SEQ_TRACK_COUNT,
                                                   &member_count) == 0U)
            || (member_count == 0U))
    {
        return 0U;
    }

    if (member_count > 8U)
    {
        member_count = 8U;
    }
    if ((out_members != NULL) && (out_capacity < member_count))
    {
        return 0U;
    }
    for (uint8_t i = 0U; (out_members != NULL) && (i < member_count); ++i)
    {
        out_members[i] = members[i];
    }
    *out_count = member_count;
    return 1U;
}

static void ui_page_template_play_set_group_slot(uint8_t slot, uint8_t member_track, uint8_t absolute_index)
{
    char *title = NULL;
    char *label = NULL;
    static char titles[4][12];
    static char labels[4][5];

    if (slot >= 4U)
    {
        return;
    }

    title = titles[slot];
    label = labels[slot];
    (void)snprintf(title, sizeof(titles[slot]), "Track %u", (unsigned int)(member_track + 1U));
    (void)snprintf(label, sizeof(labels[slot]), "T%u", (unsigned int)(member_track + 1U));

    g_ui_template_play_group_family.nav_labels[slot] = label;
    g_ui_template_play_group_family.subpages[slot].title = title;
    g_ui_template_play_group_family.subpages[slot].param_bank.params[0] = PARAM_SEQ_PLAY_V1_NOTE;
    g_ui_template_play_group_family.subpages[slot].param_bank.params[1] = PARAM_SEQ_PLAY_V1_VEL;
    g_ui_template_play_group_family.subpages[slot].param_bank.params[2] = PARAM_SEQ_PLAY_V1_LEN;
    g_ui_template_play_group_family.subpages[slot].param_bank.params[3] = PARAM_SEQ_PLAY_V1_MICTIM;
    (void)absolute_index;
}

static const ui_template_family_t *ui_page_template_play_resolve_group_family(uint8_t member_count,
                                                                              const uint8_t *members)
{
    if ((member_count <= 1U) || (members == NULL))
    {
        return NULL;
    }

    if (member_count <= 4U)
    {
        g_ui_template_play_subset = 0U;
    }
    else if (g_ui_template_play_subset > 1U)
    {
        g_ui_template_play_subset = 0U;
    }

    g_ui_template_play_group_family.family_title = (member_count <= 4U)
        ? "PLAY"
        : ((g_ui_template_play_subset == 0U) ? "PLAY 1/2" : "PLAY 2/2");
    g_ui_template_play_group_family.default_subpage = 0U;

    const uint8_t first = (uint8_t)(g_ui_template_play_subset * 4U);
    for (uint8_t slot = 0U; slot < 4U; ++slot)
    {
        const uint8_t member_index = (uint8_t)(first + slot);
        if (member_index < member_count)
        {
            ui_page_template_play_set_group_slot(slot, members[member_index], member_index);
        }
        else
        {
            g_ui_template_play_group_family.nav_labels[slot] = "-";
            g_ui_template_play_group_family.subpages[slot].title = "-";
            g_ui_template_play_group_family.subpages[slot].param_bank.params[0] = PARAM_COUNT;
            g_ui_template_play_group_family.subpages[slot].param_bank.params[1] = PARAM_COUNT;
            g_ui_template_play_group_family.subpages[slot].param_bank.params[2] = PARAM_COUNT;
            g_ui_template_play_group_family.subpages[slot].param_bank.params[3] = PARAM_COUNT;
        }
    }

    return &g_ui_template_play_group_family;
}

static const ui_template_family_t *ui_page_template_play_resolve_family(void)
{
    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(ui_get_active_track(), &role_u8);
    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        return &g_ui_template_play_slave_proxy_family;
    }

    uint8_t members[8];
    uint8_t member_count = 0U;
    if ((ui_page_template_play_collect_active_group(members, (uint8_t)(sizeof(members) / sizeof(members[0])), &member_count) != 0U)
            && (member_count > 1U))
    {
        return ui_page_template_play_resolve_group_family(member_count, members);
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

    uint8_t member_count = 0U;
    if ((ui_page_template_play_collect_active_group(NULL, 0U, &member_count) != 0U)
            && (member_count > 1U))
    {
        const uint8_t first = (uint8_t)(g_ui_template_play_subset * 4U);
        return ((uint8_t)(first + subpage_index) < member_count) ? 1U : 0U;
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

static ui_template_custom_widget_kind_t ui_page_template_play_pick_custom_widget(uint8_t slot,
                                                                                 const ui_template_subpage_t *subpage,
                                                                                 param_id_t id)
{
    (void)subpage;
    if ((slot == 0U)
            && ((id == PARAM_SEQ_PLAY_V1_NOTE)
                || (id == PARAM_SEQ_PLAY_V2_NOTE)
                || (id == PARAM_SEQ_PLAY_V3_NOTE)
                || (id == PARAM_SEQ_PLAY_V4_NOTE)))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_PLAY_NOTE;
    }

    return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static ui_template_page_state_t g_ui_template_play_state = {
    .family = 0,
    .family_resolver = ui_page_template_play_resolve_family,
    .custom_widget_picker = ui_page_template_play_pick_custom_widget,
    .subpage_enabled = ui_page_template_play_subpage_enabled,
    .virtual_slot_text = ui_page_template_play_virtual_slot_text,
    .active_subpage = 0U,
    .has_visited = 0U,
};

uint8_t ui_page_template_play_resolve_param_track(param_id_t param, uint8_t active_track, uint8_t *out_track)
{
    ui_page_template_play_context_t context;
    if (ui_page_template_play_resolve_context(param, active_track, &context) == 0U)
    {
        return 0U;
    }

    *out_track = context.target_track;
    return 1U;
}

uint8_t ui_page_template_play_resolve_context(param_id_t param,
                                              uint8_t active_track,
                                              ui_page_template_play_context_t *out_context)
{
    if ((out_context == NULL)
            || (active_track >= SEQ_TRACK_COUNT)
            || (ui_page_get_id() != UI_PAGE_TEMPLATE_PLAY)
            || (ui_page_template_play_is_play_param(param) == 0U))
    {
        return 0U;
    }

    uint8_t members[8];
    uint8_t member_count = 0U;
    if ((ui_page_template_play_collect_active_group(members, (uint8_t)(sizeof(members) / sizeof(members[0])), &member_count) == 0U)
            || (member_count <= 1U))
    {
        return 0U;
    }

    const uint8_t index = (uint8_t)((g_ui_template_play_subset * 4U) + g_ui_template_play_state.active_subpage);
    if (index >= member_count)
    {
        return 0U;
    }

    out_context->owner_track = active_track;
    out_context->member_index = index;
    out_context->target_track = members[index];
    out_context->base_param = param;
    return 1U;
}

void ui_page_template_play_open_primary(void)
{
    g_ui_template_play_subset = 0U;
    g_ui_template_play_state.resolved_family = ui_page_template_play_resolve_family();
    ui_template_page_select_subpage(&g_ui_template_play_state, 0U);
}

void ui_page_template_play_toggle_subset(void)
{
    uint8_t member_count = 0U;
    if ((ui_page_template_play_collect_active_group(NULL, 0U, &member_count) == 0U)
            || (member_count <= 4U))
    {
        g_ui_template_play_subset = 0U;
        ui_template_page_select_subpage(&g_ui_template_play_state, g_ui_template_play_state.active_subpage);
        return;
    }

    const uint8_t previous_subpage = g_ui_template_play_state.active_subpage;
    g_ui_template_play_subset = (g_ui_template_play_subset == 0U) ? 1U : 0U;
    g_ui_template_play_state.resolved_family = ui_page_template_play_resolve_family();
    if (ui_page_template_play_subpage_enabled(previous_subpage) != 0U)
    {
        ui_template_page_select_subpage(&g_ui_template_play_state, previous_subpage);
        return;
    }

    ui_template_page_select_subpage(&g_ui_template_play_state, 0U);
}

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
