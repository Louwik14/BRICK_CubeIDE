#include "ui_renderer_template.h"

#include <stdio.h>
#include <string.h>

#include "cpu_load.h"
#include "drv_display.h"
#include "font.h"
#include "param_registry.h"
#include "param_store.h"
#include "ui_core.h"
#include "ui_macro_interaction.h"
#include "ui_param.h"
#include "ui_widgets.h"
#include "Core/track_runtime.h"
#include "Storage/project_v1.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Mod/mod_lfo_v1.h"

#define UI_TEMPLATE_FRAME_W          31
#define UI_TEMPLATE_FRAME_H          37
#define UI_TEMPLATE_FRAME_Y          16
#define UI_TEMPLATE_FOOTER_Y         54
#define UI_TEMPLATE_FOOTER_H         10
#define UI_TEMPLATE_NOTE_X           101
#define UI_TEMPLATE_NOTE_Y           1

static void ui_renderer_template_format_active_pattern_label(char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    ui_pattern_stub_state_t pattern_state;
    memset(&pattern_state, 0, sizeof(pattern_state));
    ui_get_pattern_stub_state(&pattern_state);

    const char bank = (char)('A' + (pattern_state.active_bank & 0x0FU));
    (void)snprintf(out, out_len, "%c-%02u", bank, (unsigned int)(pattern_state.active_pattern + 1U));
}

static const uint8_t g_ui_template_frame_x[4] = {0U, 32U, 65U, 97U};
static const uint8_t g_ui_template_footer_x[4] = {0U, 32U, 65U, 97U};
static const uint8_t g_ui_template_footer_w[4] = {31U, 31U, 31U, 31U};
static const char *const g_ui_template_midi_note_names[12] = {"C", "C#", "D", "D#", "E", "F",
                                                               "F#", "G", "G#", "A", "A#", "B"};

static void ui_renderer_template_format_semitones(float value, char *out, uint32_t out_len)
{
    const int32_t semitones = (int32_t)((value >= 0.0f) ? (value + 0.5f) : (value - 0.5f));
    if (semitones == 0)
    {
        (void)snprintf(out, out_len, "0 st");
        return;
    }

    (void)snprintf(out, out_len, "%+ld st", (long)semitones);
}

static float ui_renderer_template_clamp(float value, float min, float max)
{
    if (value < min)
    {
        return min;
    }

    if (value > max)
    {
        return max;
    }

    return value;
}

static void ui_renderer_template_format_value(param_id_t id, float value, char *out, uint32_t out_len)
{
    const param_desc_t *desc = &param_registry[id];

    if (id == PARAM_CFG_TRACK)
    {
        (void)snprintf(out, out_len, "%s", ui_get_track_family_display_name((ui_track_family_t)((uint8_t)(value + 0.5f))));
        return;
    }

    if (id == PARAM_CFG_TRACK_TYPE)
    {
        const ui_track_family_t active_family = ui_get_track_family(ui_get_active_track());
        const ui_track_type_t active_type = ui_get_track_type_from_family_index(active_family, (uint8_t)(value + 0.5f));
        (void)snprintf(out, out_len, "%s", ui_get_track_type_display_name(active_family, active_type));
        return;
    }

    if (id == PARAM_CFG_MIDI_CH)
    {
        const uint8_t channel = (uint8_t)(value + 0.5f);
        const uint8_t duplicate = ui_track_midi_channel_used_by_other(ui_get_active_track(), channel);
        (void)snprintf(out, out_len, "%u%s", (unsigned int)channel, (duplicate != 0U) ? "*" : "");
        return;
    }

    if ((id == PARAM_LFO1_DEST) || (id == PARAM_LFO2_DEST))
    {
        if (mod_lfo_v1_dest_label(ui_get_active_track(), (uint16_t)(value + 0.5f), out, out_len) != 0U)
        {
            return;
        }
    }

    if ((id == PARAM_SEQ_PLAY_V1_VEL) || (id == PARAM_SEQ_PLAY_V2_VEL) || (id == PARAM_SEQ_PLAY_V3_VEL) || (id == PARAM_SEQ_PLAY_V4_VEL))
    {
        if (value < 0.5f)
        {
            (void)snprintf(out, out_len, "OFF");
            return;
        }
    }

    if ((id == PARAM_SEQ_PLAY_V1_NOTE) || (id == PARAM_SEQ_PLAY_V2_NOTE) || (id == PARAM_SEQ_PLAY_V3_NOTE) || (id == PARAM_SEQ_PLAY_V4_NOTE))
    {
        int32_t note = (int32_t)(value + 0.5f);
        if (note < 0)
        {
            note = 0;
        }
        if (note > 127)
        {
            note = 127;
        }

        const int32_t note_index = note % 12;
        const int32_t octave = (note / 12) - 1;
        (void)snprintf(out, out_len, "%s%ld", g_ui_template_midi_note_names[note_index], (long)octave);
        return;
    }

    if (id == PARAM_PLAITS_FREQUENCY_RANGE)
    {
        static const int8_t k_range_semitones[4] = {-24, -12, 0, 12};
        const uint8_t index = (uint8_t)(ui_renderer_template_clamp(value, 0.0f, 1.0f) * 3.0f + 0.5f);
        ui_renderer_template_format_semitones((float)k_range_semitones[index], out, out_len);
        return;
    }

    if ((desc->display_type == PARAM_DISPLAY_INT)
            && (desc->unit != NULL)
            && (strcmp(desc->unit, "st") == 0))
    {
        ui_renderer_template_format_semitones(value, out, out_len);
        return;
    }

    switch (desc->display_type)
    {
        case PARAM_DISPLAY_BOOL:
        {
            const uint32_t index = (value >= 0.5f) ? 1U : 0U;
            const char *label = ((desc->labels != NULL) && (desc->labels[index] != NULL)) ? desc->labels[index] : ((index != 0U) ? "On" : "Off");
            (void)snprintf(out, out_len, "%s", label);
            break;
        }

        case PARAM_DISPLAY_ENUM:
        {
            const int32_t index = (int32_t)(value + 0.5f);
            const char *label = NULL;
            if ((desc->labels != NULL) && (index >= 0))
            {
                label = desc->labels[index];
            }
            if (label != NULL)
            {
                (void)snprintf(out, out_len, "%s", label);
            }
            else
            {
                (void)snprintf(out, out_len, "%ld", (long)index);
            }
            break;
        }

        case PARAM_DISPLAY_PERCENT:
            (void)snprintf(out, out_len, "%3lu%%", (unsigned long)(value * 100.0f + 0.5f));
            break;

        case PARAM_DISPLAY_DB:
            (void)snprintf(out, out_len, "%.1f%s", (double)value, (desc->unit != NULL) ? desc->unit : "");
            break;

        case PARAM_DISPLAY_TIME_MS:
            (void)snprintf(out, out_len, "%.1fms", (double)(value * 1000.0f));
            break;

        case PARAM_DISPLAY_RATIO:
            (void)snprintf(out, out_len, "%.2f", (double)value);
            break;

        case PARAM_DISPLAY_INT:
            (void)snprintf(out, out_len, "%ld", (long)(value + 0.5f));
            break;

        default:
            if ((desc->unit != NULL) && (desc->unit[0] != '\0'))
            {
                (void)snprintf(out, out_len, "%.2f%s", (double)value, desc->unit);
            }
            else
            {
                (void)snprintf(out, out_len, "%.2f", (double)value);
            }
            break;
    }
}

static void ui_renderer_template_format_cpu_avg(char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if (cpu_load_is_valid() == 0U)
    {
        (void)snprintf(out, out_len, "--%%");
        return;
    }

    const uint32_t avg_permille = cpu_load_get_avg_permille();
    const uint32_t whole = avg_permille / 10U;
    const uint32_t tenth = avg_permille % 10U;
    (void)snprintf(out, out_len, "%lu.%1lu%%", (unsigned long)whole, (unsigned long)tenth);
}

static int ui_renderer_template_center_x(int x, int w, const char *txt)
{
    const int text_w = (int)drv_display_text_width(txt);
    int out = x + ((w - text_w) / 2);
    if (out < (x + 1))
    {
        out = x + 1;
    }
    return out;
}

static void ui_renderer_template_draw_open_corner_frame(int x, int y, int w, int h)
{
    const int c = 2;

    drv_display_draw_line(x + c,         y,             x + w - 1 - c, y);
    drv_display_draw_line(x + c,         y + h - 1,     x + w - 1 - c, y + h - 1);

    drv_display_draw_line(x,             y + c,         x,             y + h - 1 - c);
    drv_display_draw_line(x + w - 1,     y + c,         x + w - 1,     y + h - 1 - c);

    drv_display_draw_line(x,             y + c,         x + c,         y);
    drv_display_draw_line(x + w - 1 - c, y,             x + w - 1,     y + c);

    drv_display_draw_line(x,             y + h - 1 - c, x + c,         y + h - 1);
    drv_display_draw_line(x + w - 1 - c, y + h - 1,     x + w - 1,     y + h - 1 - c);
}

static void ui_renderer_template_draw_note_icon(int x, int y)
{
    drv_display_draw_line(x + 2, y + 1, x + 2, y + 10);
    drv_display_draw_line(x + 2, y + 1, x + 7, y + 3);
    drv_display_draw_line(x + 7, y + 3, x + 7, y + 11);
    drv_display_draw_line(x + 2, y + 6, x + 7, y + 8);
    drv_display_draw_rect(x, y + 8, 3, 3);
    drv_display_draw_rect(x + 5, y + 10, 3, 3);
}

static void ui_renderer_template_draw_inverted_label(uint8_t x, uint8_t y, const char *txt, const font_t *font)
{
    drv_display_set_font(font);
    const uint8_t w = (uint8_t)(drv_display_text_width(txt) + 2U);
    const uint8_t h = (uint8_t)(drv_display_font_height() + 2U);
    drv_display_fill_rect(x, y, w, h);
    drv_display_draw_text_inverted((uint8_t)(x + 1U), (uint8_t)(y + 1U), txt);
}

static uiw_widget_type_t ui_renderer_template_resolve_widget_type(const ui_template_page_state_t *state,
                                                                  uint8_t slot,
                                                                  param_id_t id,
                                                                  const param_desc_t *desc,
                                                                  const char *enum_label,
                                                                  const char *value_label)
{
    uiw_widget_type_t widget = uiw_pick_widget_type(desc, enum_label);

    if ((state != NULL) && (state->widget_picker != NULL))
    {
        widget = state->widget_picker(slot, id, value_label, widget);
    }

    return widget;
}

static float ui_renderer_template_get_param_display_value(param_id_t id)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            || (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
    {
        return param_get(id);
    }

    return param_store_get_active(id);
}

static void ui_renderer_template_draw_param_slot(const ui_template_page_state_t *state,
                                                 const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                 param_id_t macro_slot_param,
                                                 uint8_t slot,
                                                 param_id_t id)
{
    const int x = g_ui_template_frame_x[slot];
    const int y = UI_TEMPLATE_FRAME_Y;
    const uint8_t slot_locked = (uint8_t)((macro_slot_param < PARAM_COUNT) && (macro_slot_param == id));

    if (slot_locked != 0U)
    {
        drv_display_fill_rect(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);
        drv_display_set_draw_color(0U);
    }

    ui_renderer_template_draw_open_corner_frame(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);

    if (id >= PARAM_COUNT)
    {
        if (slot_locked != 0U)
        {
            drv_display_set_draw_color(1U);
        }

        if ((state != NULL) && (state->virtual_slot_text != NULL))
        {
            char virt_name[24];
            char virt_value[20];
            const uint8_t has_virtual = state->virtual_slot_text(slot,
                                                                 virt_name,
                                                                 (uint32_t)sizeof(virt_name),
                                                                 virt_value,
                                                                 (uint32_t)sizeof(virt_value));
            if (has_virtual != 0U)
            {
                drv_display_set_font(&FONT_4X6);
                drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, virt_name),
                                      (uint8_t)(y + 3),
                                      virt_name);
                uiw_draw_enum_text(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, virt_value);
                drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, virt_value),
                                      (uint8_t)(y + UI_TEMPLATE_FRAME_H - 8),
                                      virt_value);
                return;
            }
        }

        drv_display_set_font(&FONT_4X6);
        drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, "-"), (uint8_t)(y + 14), "-");
        return;
    }

    const param_desc_t *desc = &param_registry[id];
    float value = ui_renderer_template_get_param_display_value(id);
    uint8_t draw_name_inverted = 0U;
    uint8_t macro_slot_track = PROJECT_V1_MACRO_SLOT_TRACK_NONE;
    param_id_t macro_slot_value_param = PARAM_COUNT;
    float macro_slot_scene_value = 0.0f;
    const uint8_t has_macro_slot_value =
        ui_macro_interaction_get_active_slot_value(&macro_slot_track,
                                                   &macro_slot_value_param,
                                                   &macro_slot_scene_value);
    const uint8_t macro_value_visible =
        (uint8_t)((has_macro_slot_value != 0U)
                && (macro_slot_track == ui_get_active_track())
                && (macro_slot_value_param == id));

    if (macro_value_visible != 0U)
    {
        value = macro_slot_scene_value;
    }
    else
    {
        (void)ui_param_try_get_seq_plock_feedback_with_frame(plock_frame_ctx, id, &value, &draw_name_inverted);
    }

    const char *enum_label = NULL;
    char value_txt[20];

    if ((desc->display_type == PARAM_DISPLAY_ENUM) && (desc->labels != NULL))
    {
        const int32_t index = (int32_t)(value + 0.5f);
        if (index >= 0)
        {
            enum_label = desc->labels[index];
        }
    }

    ui_renderer_template_format_value(id, value, value_txt, (uint32_t)sizeof(value_txt));

    drv_display_set_font(&FONT_4X6);
    if (slot_locked != 0U)
    {
        drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, desc->name),
                              (uint8_t)(y + 3),
                              desc->name);
    }
    else if (draw_name_inverted != 0U)
    {
        ui_renderer_template_draw_inverted_label((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, desc->name),
                                                 (uint8_t)(y + 2),
                                                 desc->name,
                                                 &FONT_4X6);
    }
    else
    {
        drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, desc->name), (uint8_t)(y + 3), desc->name);
    }

    const uiw_widget_type_t widget_type = ui_renderer_template_resolve_widget_type(state, slot, id, desc, enum_label, value_txt);

    switch (widget_type)
    {
        case UIW_WIDGET_EMPTY:
            break;

        case UIW_WIDGET_SWITCH:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_switch(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, (value >= 0.5f) ? 1U : 0U);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_ENUM_TEXT:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            uiw_draw_enum_text(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, (enum_label != NULL) ? enum_label : value_txt);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            break;

        case UIW_WIDGET_JACK:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_jack_icon(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_KEYBOARD:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_keyboard_icon(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_WAVE_ICON:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_wave_icon(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, enum_label);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_FILTER_ICON:
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            uiw_draw_filter_icon(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, enum_label);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;

        case UIW_WIDGET_KNOB:
        default:
        {
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            const int vmin = (int)(desc->min * 10.0f);
            const int vmax = (int)(desc->max * 10.0f);
            const int vint = (int)(value * 10.0f);
            uiw_draw_knob(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, vint, vmin, vmax);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;
        }
    }

    if (slot_locked != 0U)
    {
        drv_display_set_draw_color(0U);
    }

    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, value_txt), (uint8_t)(y + UI_TEMPLATE_FRAME_H - 8), value_txt);

    if (slot_locked != 0U)
    {
        drv_display_set_draw_color(1U);
    }

}

static void ui_renderer_template_draw_header(const ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);
    const char *family_title = ((family != NULL) && (family->family_title != NULL)) ? family->family_title : "TEMPLATE";
    const uint8_t active_track = ui_get_active_track();
    char track_label[4];
    char runtime_label[12];
    char cpu_avg_label[16];
    char bpm_label[12];

    (void)snprintf(track_label, sizeof(track_label), "%u", (unsigned int)(active_track + 1U));
    ui_get_track_runtime_header_label(active_track, runtime_label, (uint32_t)sizeof(runtime_label));
    ui_renderer_template_format_cpu_avg(cpu_avg_label, (uint32_t)sizeof(cpu_avg_label));
    uint8_t draw_bpm = 0U;
    uint8_t bpm_inverted = 0U;
    uint32_t bpm_milli = 0U;

    if (seq_runtime_get_clock_source() == SEQ_CLOCK_SRC_INTERNAL)
    {
        bpm_milli = seq_runtime_get_tempo_bpm_milli();
        draw_bpm = 1U;
        bpm_inverted = 0U;
    }
    else if (seq_runtime_is_external_tempo_valid() != 0U)
    {
        bpm_milli = seq_runtime_get_external_tempo_bpm_milli();
        draw_bpm = 1U;
        bpm_inverted = 1U;
    }

    if (draw_bpm != 0U)
    {
        (void)snprintf(bpm_label,
                       sizeof(bpm_label),
                       "%lu.%01lu",
                       (unsigned long)(bpm_milli / 1000U),
                       (unsigned long)((bpm_milli % 1000U) / 100U));
    }

    drv_display_set_font(&FONT_5X7);
    ui_renderer_template_draw_inverted_label(0U, 1U, track_label, &FONT_5X7);
    const uint8_t track_label_shift = (active_track >= 9U) ? 4U : 0U;
    drv_display_draw_text((uint8_t)(9U + track_label_shift), 1U, runtime_label);

    drv_display_set_font(&FONT_4X6);
    const char *hall_mode_label = ui_get_hall_mode_short_label();
    const char *hall_mode_suffix = ui_get_hall_mode_suffix_label();
    drv_display_draw_text((uint8_t)(9U + track_label_shift), 9U, hall_mode_label);
    if ((hall_mode_suffix != NULL) && (hall_mode_suffix[0] != '\0'))
    {
        const uint8_t suffix_x = (uint8_t)(9U + track_label_shift + drv_display_text_width(hall_mode_label) + 2U);
        drv_display_draw_text(suffix_x, 9U, hall_mode_suffix);
    }

    drv_display_set_font(&FONT_5X7);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(0, OLED_WIDTH, family_title), 2U, family_title);

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(0, OLED_WIDTH, cpu_avg_label), 9U, cpu_avg_label);
    ui_renderer_template_draw_note_icon(UI_TEMPLATE_NOTE_X, UI_TEMPLATE_NOTE_Y);
    if (draw_bpm != 0U)
    {
        if (bpm_inverted != 0U)
        {
            ui_renderer_template_draw_inverted_label(109U, 1U, bpm_label, &FONT_5X7);
        }
        else
        {
            drv_display_draw_text(109U, 1U, bpm_label);
        }
    }
    char pattern_label[6];
    ui_renderer_template_format_active_pattern_label(pattern_label, sizeof(pattern_label));
    drv_display_draw_text(113U, 9U, pattern_label);
}

static void ui_renderer_template_draw_footer(const ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);

    drv_display_set_font(&FONT_4X6);

    for (uint8_t i = 0U; i < 4U; i++)
    {
        const int bx = g_ui_template_footer_x[i];
        const int bw = g_ui_template_footer_w[i];
        const uint8_t subpage_enabled = ((state->subpage_enabled == NULL) || (state->subpage_enabled(i) != 0U)) ? 1U : 0U;
        const char *label = family->nav_labels[i];

        if (subpage_enabled == 0U)
        {
            label = "-";
        }

        if ((label == NULL) || (label[0] == '\0'))
        {
            label = "-";
        }


        const int x_label = ui_renderer_template_center_x(bx, bw, label);

        if (i == state->active_subpage)
        {
            drv_display_fill_rect(bx, UI_TEMPLATE_FOOTER_Y, bw, UI_TEMPLATE_FOOTER_H);

            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx + 1,             UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y + 1,             false);

            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx + bw - 2,        UI_TEMPLATE_FOOTER_Y,                 false);
            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y + 1,             false);

            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx + 1,             UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx,                 UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 2, false);

            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx + bw - 2,        UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 1, false);
            drv_display_draw_pixel(bx + bw - 1,        UI_TEMPLATE_FOOTER_Y + UI_TEMPLATE_FOOTER_H - 2, false);

            drv_display_draw_text_inverted((uint8_t)x_label, 56U, label);
        }
        else
        {
            ui_renderer_template_draw_open_corner_frame(bx, UI_TEMPLATE_FOOTER_Y, bw, UI_TEMPLATE_FOOTER_H);
            drv_display_draw_text((uint8_t)x_label, 56U, label);
        }
    }
}

void ui_renderer_template_draw(const ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);

    if ((state == NULL) || (family == NULL))
    {
        return;
    }

    ui_renderer_template_draw_header(state);

    ui_param_seq_plock_feedback_frame_t plock_frame_ctx;
    ui_param_seq_plock_feedback_frame_begin(&plock_frame_ctx);
    param_id_t macro_slot_param = PARAM_COUNT;
    (void)ui_macro_interaction_get_active_slot_lock(&macro_slot_param);

    const ui_template_subpage_t *subpage = ui_template_page_get_active_subpage(state);
    if (subpage != NULL)
    {
        for (uint8_t i = 0U; i < 4U; i++)
        {
            ui_renderer_template_draw_param_slot(state, &plock_frame_ctx, macro_slot_param, i, subpage->param_bank.params[i]);
        }
    }

    ui_renderer_template_draw_footer(state);

    drv_display_set_font(&FONT_5X7);
}
