#include "pages/ui_page_template_seq.h"

#include <stdio.h>

#include "App/control_domain.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_division_catalog.h"
#include "Seq/seq_model.h"
#include "Seq/seq_runtime_control.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_seq_family = {
    .family_title = "SEQ",
    .nav_labels = { "SEQ", "-", "-", "-" },
    .subpages = {
        { .title = "SEQ", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "-", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
    },
    .default_subpage = 0U,
};

static const ui_template_family_t *ui_page_template_seq_resolve_family(void)
{
    return ui_template_family_resolve_active_track(UI_TEMPLATE_FAMILY_SEQ);
}

static uint8_t ui_page_template_seq_virtual_slot_text(uint8_t slot,
    char *out_name, uint32_t out_name_len, char *out_value, uint32_t out_value_len);

static ui_template_page_state_t g_ui_template_seq_state = {
    .family = 0,
    .family_resolver = ui_page_template_seq_resolve_family,
    .virtual_slot_text = ui_page_template_seq_virtual_slot_text,
    .active_subpage = 0U,
    .has_visited = 0U,
};

static uint8_t ui_page_template_seq_virtual_slot_text(uint8_t slot,
    char *out_name, uint32_t out_name_len, char *out_value, uint32_t out_value_len)
{
    static const char *const names[] = { "LENGTH", "DIV", "QUANT", "SWING" };
    const seq_track_id_t track = (seq_track_id_t)ui_get_active_lane();
    uint8_t value = 0U;
    if (slot >= 4U) return 0U;
    if (slot == 0U) value = seq_model_get_track_length(track);
    else if (slot == 1U) (void)seq_runtime_get_track_div(track, &value);
    else if (slot == 2U) (void)seq_runtime_get_track_quant(track, &value);
    else (void)seq_runtime_get_track_swing(track, &value);
    (void)snprintf(out_name, out_name_len, "%s", names[slot]);
    if (slot == 1U) (void)snprintf(out_value, out_value_len, "%s",
                                  seq_division_track_labels[seq_division_track_div_to_ui(value)]);
    else if (slot >= 2U) (void)snprintf(out_value, out_value_len, "%u%%", (unsigned)value);
    else (void)snprintf(out_value, out_value_len, "%u", (unsigned)value);
    return 1U;
}

uint8_t ui_page_template_seq_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((ui_page_get_id() != UI_PAGE_TEMPLATE_SEQ) || (encoder >= 4U) || (delta == 0)) return 0U;
    const seq_track_id_t track = (seq_track_id_t)ui_get_active_lane();
    int32_t value = 0;
    if (encoder == 0U)
    {
        value = (int32_t)seq_model_get_track_length(track) + delta;
        if (value < 1) value = 1;
        if (value > SEQ_MAX_STEPS) value = SEQ_MAX_STEPS;
        const control_seq_intent_t intent = {
            .operation = CONTROL_SEQ_SET_LENGTH,
            .track = (uint8_t)track,
            .step = (uint8_t)value
        };
        return control_domain_request_seq(&intent);
    }
    else if (encoder == 1U)
    {
        uint8_t div = 1U; (void)seq_runtime_get_track_div(track, &div);
        uint8_t index = seq_division_track_div_to_ui(div);
        value = (int32_t)index + ((delta > 0) ? 1 : -1);
        if (value < 0) value = 0;
        if (value > 3) value = 3;
        const control_seq_intent_t intent = {
            .operation = CONTROL_SEQ_SET_DIVISION,
            .track = (uint8_t)track,
            .step = seq_division_track_div_from_ui((uint8_t)value)
        };
        return control_domain_request_seq(&intent);
    }
    else
    {
        uint8_t current = 0U;
        if (encoder == 2U) (void)seq_runtime_get_track_quant(track, &current);
        else (void)seq_runtime_get_track_swing(track, &current);
        value = (int32_t)current + delta;
        if (value < 0) value = 0;
        if (value > 100) value = 100;
        const control_seq_intent_t intent = {
            .operation = (encoder == 2U) ? CONTROL_SEQ_SET_QUANTIZATION : CONTROL_SEQ_SET_SWING,
            .track = (uint8_t)track,
            .step = (uint8_t)value
        };
        return control_domain_request_seq(&intent);
    }
    return 1U;
}

void ui_page_template_seq_register_families(void)
{
    for (uint8_t track_family = 0U; track_family < (uint8_t)TRACK_FAMILY_COUNT; ++track_family)
    {
        for (uint8_t track_type = 0U; track_type < (uint8_t)TRACK_TYPE_COUNT; ++track_type)
        {
            if (!ui_track_type_is_valid_for_family((track_family_t)track_family, (track_type_t)track_type))
            {
                continue;
            }

            ui_template_family_register(UI_TEMPLATE_FAMILY_SEQ,
                                        (track_family_t)track_family,
                                        (track_type_t)track_type,
                                        &g_ui_template_seq_family);
        }
    }
}

const ui_page_t g_ui_page_template_seq = {
    .enter = ui_template_page_enter,
    .leave = ui_template_page_leave,
    .handle_event = ui_template_page_handle_event,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_seq_state,
};
