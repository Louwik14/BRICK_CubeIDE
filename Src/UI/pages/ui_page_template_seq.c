#include "pages/ui_page_template_seq.h"

#include <stdio.h>

#include "Seq/seq_edit.h"
#include "Seq/seq_division_catalog.h"
#include "Seq/seq_model.h"
#include "Seq/seq_runtime_control.h"
#include "Storage/groove_bank.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"

static const ui_template_family_t g_ui_template_seq_family = {
    .family_title = "SEQ",
    .nav_labels = { "COMMON", "GROOVE 1", "GROOVE 2", "-" },
    .subpages = {
        { .title = "COMMON", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "GROOVE 1", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
        { .title = "GROOVE 2", .param_bank = { .params = { PARAM_COUNT, PARAM_COUNT, PARAM_COUNT, PARAM_COUNT } } },
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
    static const char *const base_labels[SEQ_TIMING_BASE_COUNT] = {
        "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"
    };
    static const char *const direction_labels[SEQ_DIRECTION_COUNT] = {
        "FWD", "REV", "PINGPONG", "RANDOM"
    };
    const seq_track_id_t track = (seq_track_id_t)ui_get_active_lane();
    seq_track_timing_config_t timing;
    if (slot >= 4U) return 0U;
    if (seq_runtime_get_track_timing(track, &timing) == 0U) return 0U;
    if (g_ui_template_seq_state.active_subpage == 0U)
    {
        static const char *const names[] = { "LENGTH", "DIV", "DIR", "ROTATE" };
        uint8_t division = 1U;
        uint8_t direction = (uint8_t)SEQ_DIRECTION_FWD;
        int8_t rotate = 0;
        (void)seq_runtime_get_track_div(track, &division);
        (void)seq_runtime_get_track_traversal(track, &direction, &rotate);
        (void)snprintf(out_name, out_name_len, "%s", names[slot]);
        if (slot == 0U)
            (void)snprintf(out_value, out_value_len, "%u",
                           (unsigned)seq_model_get_track_length(track));
        else if (slot == 1U)
            (void)snprintf(out_value, out_value_len, "%s",
                seq_division_track_labels[seq_division_track_div_to_ui(division)]);
        else if (slot == 2U)
            (void)snprintf(out_value, out_value_len, "%s",
                direction_labels[direction]);
        else
            (void)snprintf(out_value, out_value_len, "%+d", (int)rotate);
    }
    else if (g_ui_template_seq_state.active_subpage == 1U)
    {
        static const char *const names[] = { "TEMPLATE", "GLOBAL", "QUANT", "BASE" };
        (void)snprintf(out_name, out_name_len, "%s", names[slot]);
        if (slot == 0U)
        {
            if((timing.groove_flags&SEQ_GROOVE_FLAG_MISSING)!=0U)
                (void)snprintf(out_value,out_value_len,"MISSING:%.20s",timing.groove_name);
            else if(timing.groove==SEQ_GROOVE_NONE)
                (void)snprintf(out_value,out_value_len,"OFF");
            else
            {
                groove_bank_entry_view_t entry;
                if(groove_bank_get(timing.groove,&entry))
                    (void)snprintf(out_value,out_value_len,"%s",entry.name);
                else
                    (void)snprintf(out_value,out_value_len,"MISSING");
            }
        }
        else if (slot == 1U)
            (void)snprintf(out_value, out_value_len, "%u%%", (unsigned)timing.global);
        else if (slot == 2U)
            (void)snprintf(out_value, out_value_len, "%u%%", (unsigned)timing.quantize);
        else
            (void)snprintf(out_value, out_value_len, "%s", base_labels[timing.base]);
    }
    else if (g_ui_template_seq_state.active_subpage == 2U)
    {
        static const char *const names[] = { "RANDOM", "VELOCITY", "TIMING", "" };
        (void)snprintf(out_name, out_name_len, "%s", names[slot]);
        if (slot == 0U)
            (void)snprintf(out_value, out_value_len, "%u%%", (unsigned)timing.random);
        else if (slot == 1U)
            (void)snprintf(out_value, out_value_len, "%+d%%", (int)timing.velocity);
        else if (slot == 2U)
            (void)snprintf(out_value, out_value_len, "%u%%", (unsigned)timing.timing);
        else
            out_value[0] = '\0';
    }
    else
    {
        out_name[0] = '\0';
        out_value[0] = '\0';
    }
    return 1U;
}

uint8_t ui_page_template_seq_handle_encoder(uint8_t encoder, int16_t delta)
{
    if ((ui_page_get_id() != UI_PAGE_TEMPLATE_SEQ) || (encoder >= 4U) || (delta == 0)) return 0U;
    const seq_track_id_t track = (seq_track_id_t)ui_get_active_lane();
    seq_track_timing_config_t timing;
    if (seq_runtime_get_track_timing(track, &timing) == 0U) return 0U;
    int32_t value = 0;
    if ((g_ui_template_seq_state.active_subpage == 0U) && (encoder == 0U))
    {
        value = (int32_t)seq_model_get_track_length(track) + delta;
        if (value < 1) value = 1;
        if (value > SEQ_MAX_STEPS) value = SEQ_MAX_STEPS;
        (void)seq_edit_set_track_length(track, (uint8_t)value);
    }
    else if ((g_ui_template_seq_state.active_subpage == 0U) && (encoder == 1U))
    {
        uint8_t div = 1U; (void)seq_runtime_get_track_div(track, &div);
        uint8_t index = seq_division_track_div_to_ui(div);
        value = (int32_t)index + ((delta > 0) ? 1 : -1);
        if (value < 0) value = 0;
        if (value > 3) value = 3;
        (void)seq_edit_set_track_division(track, seq_division_track_div_from_ui((uint8_t)value));
    }
    else if (g_ui_template_seq_state.active_subpage == 0U)
    {
        uint8_t direction = (uint8_t)SEQ_DIRECTION_FWD;
        int8_t rotate = 0;
        (void)seq_runtime_get_track_traversal(track, &direction, &rotate);
        if (encoder == 2U)
        {
            value = (int32_t)direction + ((delta > 0) ? 1 : -1);
            if (value < 0) value = 0;
            if (value >= SEQ_DIRECTION_COUNT) value = SEQ_DIRECTION_COUNT - 1;
            direction = (uint8_t)value;
        }
        else
        {
            value = (int32_t)rotate + delta;
            if (value < -(int32_t)(SEQ_MAX_STEPS - 1U))
                value = -(int32_t)(SEQ_MAX_STEPS - 1U);
            if (value > (int32_t)(SEQ_MAX_STEPS - 1U))
                value = (int32_t)(SEQ_MAX_STEPS - 1U);
            rotate = (int8_t)value;
        }
        (void)seq_edit_set_track_traversal(track, direction, rotate);
    }
    else if (g_ui_template_seq_state.active_subpage == 1U)
    {
        if (encoder == 0U)
        {
            value = (int32_t)timing.groove + ((delta > 0) ? 1 : -1);
            if (value < 0) value = 0;
            const uint8_t count=groove_bank_count();
            if (value > count) value = count;
            (void)seq_edit_select_track_groove(track,(uint8_t)value);
            return 1U;
        }
        else if (encoder == 1U)
        {
            value = (int32_t)timing.global + delta;
            if (value < 0) value = 0;
            if (value > 130) value = 130;
            timing.global = (uint8_t)value;
        }
        else if (encoder == 2U)
        {
            value = (int32_t)timing.quantize + delta;
            if (value < 0) value = 0;
            if (value > 100) value = 100;
            timing.quantize = (uint8_t)value;
        }
        else
        {
            value = (int32_t)timing.base + ((delta > 0) ? 1 : -1);
            if (value < 0) value = 0;
            if (value >= SEQ_TIMING_BASE_COUNT) value = SEQ_TIMING_BASE_COUNT - 1;
            timing.base = (uint8_t)value;
        }
        (void)seq_edit_set_track_timing(track, &timing);
    }
    else if (g_ui_template_seq_state.active_subpage == 2U)
    {
        if (encoder == 0U)
        {
            value = (int32_t)timing.random + delta;
            if (value < 0) value = 0;
            if (value > 100) value = 100;
            timing.random = (uint8_t)value;
        }
        else if (encoder == 1U)
        {
            value = (int32_t)timing.velocity + delta;
            if (value < -100) value = -100;
            if (value > 100) value = 100;
            timing.velocity = (int8_t)value;
        }
        else if (encoder == 2U)
        {
            value = (int32_t)timing.timing + delta;
            if (value < 0) value = 0;
            if (value > 100) value = 100;
            timing.timing = (uint8_t)value;
        }
        else return 0U;
        (void)seq_edit_set_track_timing(track, &timing);
    }
    else return 0U;
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
    .tick = ui_template_page_tick,
    .sync_active_context = ui_template_page_sync_active_track_context,
    .render = ui_template_page_render,
    .context = &g_ui_template_seq_state,
};
