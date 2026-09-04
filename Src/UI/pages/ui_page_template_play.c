#include "pages/ui_page_template_play.h"

#include <stddef.h>
#include <stdio.h>

#include "App/control_domain.h"
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

static uint8_t ui_page_template_play_virtual_slot_value(
    const ui_param_seq_plock_feedback_frame_t *frame_ctx,
    uint8_t slot,
    float *out_value,
    uint8_t *out_bipolar);

static uint8_t ui_page_template_play_virtual_slot_text(uint8_t slot,
                                                        char *out_name,
                                                        uint32_t out_name_len,
                                                        char *out_value,
                                                        uint32_t out_value_len)
{
    static const char *const names[] = { "NOTE", "VEL", "LEN", "MICTIM" };
    int16_t value = 0;
    float visible_value = 0.0f;
    uint8_t bipolar = 0U;
    if ((slot >= SEQ_STEP_PLAY_FIELD_COUNT)
            || (ui_page_template_play_virtual_slot_value(NULL, slot,
                                                          &visible_value,
                                                          &bipolar) == 0U)) return 0U;
    (void)bipolar;
    value = (int16_t)visible_value;
    (void)snprintf(out_name, out_name_len, "%s", names[slot]);
    if ((slot == SEQ_STEP_PLAY_FIELD_VELOCITY) && (value == 0))
    {
        (void)snprintf(out_value, out_value_len, "OFF");
    }
    else if (slot == SEQ_STEP_PLAY_FIELD_NOTE)
    {
        static const char *const note_names[] = {
            "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
        };
        int16_t note = value;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        (void)snprintf(out_value, out_value_len, "%s%d",
                       note_names[note % 12], (note / 12) - 1);
    }
    else
    {
        (void)snprintf(out_value, out_value_len, "%d", (int)value);
    }
    return 1U;
}

static ui_template_custom_widget_kind_t ui_page_template_play_pick_virtual_widget(
    uint8_t slot, const ui_template_subpage_t *subpage)
{
    (void)subpage;
    return (slot == SEQ_STEP_PLAY_FIELD_NOTE)
        ? UI_TEMPLATE_CUSTOM_WIDGET_PLAY_NOTE
        : UI_TEMPLATE_CUSTOM_WIDGET_NONE;
}

static uint8_t ui_page_template_play_virtual_slot_value(
    const ui_param_seq_plock_feedback_frame_t *frame_ctx,
    uint8_t slot,
    float *out_value,
    uint8_t *out_bipolar)
{
    (void)frame_ctx;
    if ((out_value == NULL) || (out_bipolar == NULL)
            || (slot >= SEQ_STEP_PLAY_FIELD_COUNT)) return 0U;
    const seq_track_id_t track = (seq_track_id_t)ui_get_active_lane();
    const uint8_t voice = (uint8_t)(g_ui_template_play_subset * 4U
                                     + g_ui_template_play_state.active_subpage);
    int16_t value = 0;
    seq_step_id_t held_steps[SEQ_STEPS_PER_PAGE];
    seq_track_id_t held_track = 0U;
    const uint8_t held_count = seq_edit_collect_held_steps(&held_track,
                                                            held_steps,
                                                            SEQ_STEPS_PER_PAGE,
                                                            1U);
    if ((held_count != 0U) && (held_track == track))
    {
        seq_step_id_t ref_step = held_steps[0];
        for (uint8_t i = 1U; i < held_count; ++i)
        {
            if (held_steps[i] < ref_step) ref_step = held_steps[i];
        }
        if (seq_edit_step_play_get(track, ref_step, voice,
                                       (seq_step_play_field_t)slot, &value) != 0U)
        {
            *out_value = (float)value;
            *out_bipolar = 0U;
            return 1U;
        }
    }
    if (seq_model_play_base_get(track, voice, (seq_step_play_field_t)slot, &value) == 0U)
        return 0U;
    *out_value = (float)value;
    *out_bipolar = 0U;
    return 1U;
}

static ui_template_page_state_t g_ui_template_play_state = {
    .family = 0,
    .family_resolver = ui_page_template_play_resolve_family,
    .virtual_custom_widget_picker = ui_page_template_play_pick_virtual_widget,
    .subpage_enabled = ui_page_template_play_subpage_enabled,
    .virtual_slot_text = ui_page_template_play_virtual_slot_text,
    .virtual_slot_value = ui_page_template_play_virtual_slot_value,
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
            const control_seq_intent_t intent = {
                .operation = CONTROL_SEQ_PLAY_SET,
                .track = (uint8_t)track,
                .step = (uint8_t)held_steps[i],
                .voice = voice,
                .field = (uint8_t)field,
                .value = value
            };
            (void)control_domain_request_seq(&intent);
        }
        return 1U;
    }
    int16_t value = 0;
    if (seq_model_play_base_get(track, voice, field, &value) == 0U) return 1U;
    value = (int16_t)(value + delta);
    if (field == SEQ_STEP_PLAY_FIELD_LENGTH) { if (value < 1) value = 1; if (value > 64) value = 64; }
    else if (field == SEQ_STEP_PLAY_FIELD_MICROTIMING) { if (value < -24) value = -24; if (value > 24) value = 24; }
    else { if (value < 0) value = 0; if (value > 127) value = 127; }
    const control_seq_intent_t intent = {
        .operation = CONTROL_SEQ_PLAY_SET,
        .track = (uint8_t)track,
        .step = 0xFFU,
        .voice = voice,
        .field = (uint8_t)field,
        .value = value
    };
    (void)control_domain_request_seq(&intent);
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
