#include "pages/ui_page_template_play.h"

#include <stddef.h>

#include "Core/track_runtime.h"
#include "Core/entity_topology.h"
#include "Seq/seq_model.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"

static uint8_t g_ui_template_play_subset;

static const ui_template_family_t g_ui_template_play_families[2] = {{
    .family_title = "PLAY 1/2",
    .nav_labels = { "V1", "V2", "V3", "V4" },
    .subpages = {
        { .title = "Voice 1", .param_bank = { .params = { PARAM_SEQ_PLAY_V1_NOTE, PARAM_SEQ_PLAY_V1_VEL, PARAM_SEQ_PLAY_V1_LEN, PARAM_SEQ_PLAY_V1_MICTIM } } },
        { .title = "Voice 2", .param_bank = { .params = { PARAM_SEQ_PLAY_V2_NOTE, PARAM_SEQ_PLAY_V2_VEL, PARAM_SEQ_PLAY_V2_LEN, PARAM_SEQ_PLAY_V2_MICTIM } } },
        { .title = "Voice 3", .param_bank = { .params = { PARAM_SEQ_PLAY_V3_NOTE, PARAM_SEQ_PLAY_V3_VEL, PARAM_SEQ_PLAY_V3_LEN, PARAM_SEQ_PLAY_V3_MICTIM } } },
        { .title = "Voice 4", .param_bank = { .params = { PARAM_SEQ_PLAY_V4_NOTE, PARAM_SEQ_PLAY_V4_VEL, PARAM_SEQ_PLAY_V4_LEN, PARAM_SEQ_PLAY_V4_MICTIM } } },
    },
    .default_subpage = 0U,
}, {
    .family_title = "PLAY 2/2",
    .nav_labels = { "V5", "V6", "V7", "V8" },
    .subpages = {
        { .title = "Voice 5", .param_bank = { .params = { PARAM_SEQ_PLAY_V5_NOTE, PARAM_SEQ_PLAY_V5_VEL, PARAM_SEQ_PLAY_V5_LEN, PARAM_SEQ_PLAY_V5_MICTIM } } },
        { .title = "Voice 6", .param_bank = { .params = { PARAM_SEQ_PLAY_V6_NOTE, PARAM_SEQ_PLAY_V6_VEL, PARAM_SEQ_PLAY_V6_LEN, PARAM_SEQ_PLAY_V6_MICTIM } } },
        { .title = "Voice 7", .param_bank = { .params = { PARAM_SEQ_PLAY_V7_NOTE, PARAM_SEQ_PLAY_V7_VEL, PARAM_SEQ_PLAY_V7_LEN, PARAM_SEQ_PLAY_V7_MICTIM } } },
        { .title = "Voice 8", .param_bank = { .params = { PARAM_SEQ_PLAY_V8_NOTE, PARAM_SEQ_PLAY_V8_VEL, PARAM_SEQ_PLAY_V8_LEN, PARAM_SEQ_PLAY_V8_MICTIM } } },
    },
    .default_subpage = 0U,
}};

static uint8_t ui_page_template_play_is_play_param(param_id_t param)
{
    uint8_t play_index = 0U;
    seq_step_play_field_t play_field = SEQ_STEP_PLAY_FIELD_NOTE;
    return seq_model_play_resolve_param(param, &play_index, &play_field);
}

static const ui_template_family_t *ui_page_template_play_resolve_family(void)
{
    return &g_ui_template_play_families[g_ui_template_play_subset];
}

static uint8_t ui_page_template_play_subpage_enabled(uint8_t subpage_index)
{
    const uint8_t active_track = ui_get_active_lane();
    /* Consumer-edge refresh: subpage availability is a pure projection read after refresh. */
    track_runtime_refresh_track(active_track);
    track_runtime_resolved_track_t resolved;
    if (track_runtime_resolve_track(active_track, &resolved) == 0U)
    {
        return 0U;
    }

    const uint8_t play_index = (uint8_t)(g_ui_template_play_subset * 4U + subpage_index);
    return (play_index < seq_model_play_capacity((seq_track_id_t)active_track)) ? 1U : 0U;
}

static uint8_t ui_page_template_play_virtual_slot_text(uint8_t slot,
                                                        char *out_name,
                                                        uint32_t out_name_len,
                                                        char *out_value,
                                                        uint32_t out_value_len)
{
    (void)slot; (void)out_name; (void)out_name_len; (void)out_value; (void)out_value_len;
    return 0U;
}

static ui_template_custom_widget_kind_t ui_page_template_play_pick_custom_widget(uint8_t slot,
                                                                                 const ui_template_subpage_t *subpage,
                                                                                 param_id_t id)
{
    (void)subpage;
    if ((slot == 0U)
            && (ui_page_template_play_is_play_param(id) != 0U))
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
    if ((out_track == NULL)
            || (ui_page_template_play_resolve_context(param, active_track, &context) == 0U))
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
            || (active_track >= SEQ_LANE_CAPACITY)
            || (ui_page_get_id() != UI_PAGE_TEMPLATE_PLAY)
            || (ui_page_template_play_is_play_param(param) == 0U))
    {
        return 0U;
    }

    entity_topology_descriptor_t entity;
    if ((entity_topology_get((brick_entity_id_t)active_track, &entity) == 0U)
            || (entity_topology_can_sequence(&entity) == 0U))
    {
        return 0U;
    }

    out_context->owner_track = (entity.role == ENTITY_ROLE_GROUP_CHILD)
        ? (uint8_t)entity.parent_entity_id : (uint8_t)entity.entity_id;
    out_context->member_index = (entity.role == ENTITY_ROLE_GROUP_CHILD)
        ? entity.member_index : 0U;
    out_context->target_track = (uint8_t)entity.entity_id;
    out_context->base_param = param;
    return 1U;
}

void ui_page_template_play_open_primary(void)
{
    g_ui_template_play_subset = 0U;
    g_ui_template_play_state.navigation_subset = 0U;
    g_ui_template_play_state.resolved_family = ui_page_template_play_resolve_family();
    ui_template_page_select_subpage(&g_ui_template_play_state, 0U);
}

void ui_page_template_play_toggle_subset(void)
{
    const uint8_t active_track = ui_get_active_lane();
    if (seq_model_play_capacity((seq_track_id_t)active_track) > 4U)
    {
        g_ui_template_play_subset ^= 1U;
        g_ui_template_play_state.navigation_subset = g_ui_template_play_subset;
        g_ui_template_play_state.resolved_family = ui_page_template_play_resolve_family();
    }
    ui_template_page_select_subpage(&g_ui_template_play_state, g_ui_template_play_state.active_subpage);
}

void ui_page_template_play_register_families(void)
{
    for (uint8_t family = 0U; family < (uint8_t)UI_TRACK_FAMILY_COUNT; ++family)
    {
        const ui_track_family_t track_family = (ui_track_family_t)family;
        if ((ui_track_family_is_engine(track_family) == 0U)
                && (track_family != UI_TRACK_FAMILY_MIDI)
                && (track_family != UI_TRACK_FAMILY_EXTERNAL))
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
            ui_template_family_register(UI_TEMPLATE_FAMILY_PLAY,
                                        track_family,
                                        track_type,
                                        &g_ui_template_play_families[0]);
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
