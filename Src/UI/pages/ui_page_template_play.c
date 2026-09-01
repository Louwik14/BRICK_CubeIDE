#include "pages/ui_page_template_play.h"

#include <stddef.h>
#include <stdio.h>

#include "Track/track_runtime.h"
#include "Track/entity_topology.h"
#include "Seq/seq_model.h"
#include "Seq/seq_edit.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"

static uint8_t g_ui_template_play_subset;
static ui_template_page_state_t g_ui_template_play_state;

static const ui_template_family_t g_ui_template_play_families[2] = {{
    .family_title = "PLAY 1/2",
    .nav_labels = { "V1", "V2", "V3", "V4" },
    .subpages = {
        { .title = "Voice 1", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "Voice 2", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "Voice 3", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "Voice 4", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
}, {
    .family_title = "PLAY 2/2",
    .nav_labels = { "V5", "V6", "V7", "V8" },
    .subpages = {
        { .title = "Voice 5", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "Voice 6", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "Voice 7", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "Voice 8", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
}};

static const ui_template_family_t *ui_page_template_play_resolve_family(void)
{
    return &g_ui_template_play_families[g_ui_template_play_subset];
}

static uint8_t ui_page_template_play_subpage_enabled(uint8_t subpage_index)
{
    const uint8_t active_track = ui_get_active_lane();
    /* Consumer-edge refresh: subpage availability is a pure projection read after refresh. */
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
    static const char *const names[] = { "NOTE", "VEL", "LEN", "MICTIM" };
    const seq_track_id_t track = (seq_track_id_t)ui_get_active_lane();
    const uint8_t voice = (uint8_t)(g_ui_template_play_subset * 4U
                                     + g_ui_template_play_state.active_subpage);
    int16_t value = 0;
    if ((slot >= SEQ_STEP_PLAY_FIELD_COUNT)
            || (seq_model_play_base_get(track, voice,
                    (seq_step_play_field_t)slot, &value) == 0U)) return 0U;
    (void)snprintf(out_name, out_name_len, "%s", names[slot]);
    (void)snprintf(out_value, out_value_len, "%d", (int)value);
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

uint8_t ui_page_template_play_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((ui_page_get_id() != UI_PAGE_TEMPLATE_PLAY) || (encoder >= 4U) || (delta == 0)) return 0U;
    const seq_track_id_t track = (seq_track_id_t)ui_get_active_lane();
    const uint8_t voice = (uint8_t)(g_ui_template_play_subset * 4U
                                     + g_ui_template_play_state.active_subpage);
    const seq_step_play_field_t field = (seq_step_play_field_t)encoder;
    seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
    seq_track_id_t held_track = 0U;
    const uint8_t held_count = seq_edit_collect_held_steps(&held_track,
                                                           held_steps,
                                                           SEQ_STEPS_PER_PAGE,
                                                           1U);
    if ((held_count != 0U) && (held_track == track))
    {
        for (uint8_t i = 0U; i < held_count; ++i)
        {
            int16_t value = 0;
            if ((seq_edit_step_play_get(track, held_steps[i], voice, field, &value) == 0U)
                    && (seq_model_play_base_get(track, voice, field, &value) == 0U)) continue;
            value = (int16_t)(value + delta);
            if (field == SEQ_STEP_PLAY_FIELD_LENGTH) { if (value < 1) value = 1; if (value > 64) value = 64; }
            else if (field == SEQ_STEP_PLAY_FIELD_MICROTIMING) { if (value < -24) value = -24; if (value > 24) value = 24; }
            else { if (value < 0) value = 0; if (value > 127) value = 127; }
            const seq_plock_op_status_t status = seq_edit_step_play_upsert(
                track, held_steps[i], voice, field, value);
            if ((status == SEQ_PLOCK_OP_CREATED) || (status == SEQ_PLOCK_OP_UPDATED))
                seq_edit_step_play_commit(track, held_steps[i], voice, field);
        }
        return 1U;
    }
    int16_t value = 0;
    if (seq_model_play_base_get(track, voice, field, &value) == 0U) return 1U;
    value = (int16_t)(value + delta);
    if (field == SEQ_STEP_PLAY_FIELD_LENGTH) { if (value < 1) value = 1; if (value > 64) value = 64; }
    else if (field == SEQ_STEP_PLAY_FIELD_MICROTIMING) { if (value < -24) value = -24; if (value > 24) value = 24; }
    else { if (value < 0) value = 0; if (value > 127) value = 127; }
    (void)seq_model_play_base_set(track, voice, field, value);
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
    for (uint8_t family = 0U; family < (uint8_t)TRACK_FAMILY_COUNT; ++family)
    {
        const track_family_t track_family = (track_family_t)family;
        if ((ui_track_family_is_engine(track_family) == 0U)
                && (track_family != TRACK_FAMILY_MIDI)
                && (track_family != TRACK_FAMILY_EXTERNAL))
        {
            continue;
        }

        for (uint8_t type = 0U; type < (uint8_t)TRACK_TYPE_COUNT; ++type)
        {
            const track_type_t track_type = (track_type_t)type;
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
