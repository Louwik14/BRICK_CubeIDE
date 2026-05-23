#include "ui_renderer_template.h"

#include <stdio.h>
#include <string.h>

#include "cpu_load.h"
#include "drv_display.h"
#include "font.h"
#include "param_registry.h"
#include "ui_core.h"
#include "ui_macro_interaction.h"
#include "ui_param.h"
#include "ui_widgets.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Storage/project_v1.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Mod/mod_lfo_v1.h"

#define UI_TEMPLATE_FRAME_W          32
#define UI_TEMPLATE_FRAME_H          38
#define UI_TEMPLATE_FRAME_Y          17
#define UI_TEMPLATE_FOOTER_Y         55
#define UI_TEMPLATE_FOOTER_H         9
#define UI_TEMPLATE_FOOTER_TEXT_Y    57
#define UI_TEMPLATE_CARD_LABEL_Y     3
#define UI_TEMPLATE_CARD_VALUE_Y     (UI_TEMPLATE_FRAME_H - 9)
#define UI_TEMPLATE_CARD_LABEL_MAX_PX 28U
#define UI_TEMPLATE_HEADER_TITLE_X   43
#define UI_TEMPLATE_HEADER_TITLE_W   42

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

static const uint8_t g_ui_template_frame_x[4] = {0U, 32U, 64U, 96U};
static const uint8_t g_ui_template_footer_x[4] = {0U, 32U, 64U, 96U};
static const uint8_t g_ui_template_footer_w[4] = {32U, 32U, 32U, 32U};
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

static void ui_renderer_template_format_fixed(float value, uint8_t decimals, const char *unit, char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    int32_t scale = 1;
    for (uint8_t i = 0U; i < decimals; ++i)
    {
        scale *= 10;
    }

    float scaled_f = value * (float)scale;
    if (scaled_f > 2147483647.0f)
    {
        scaled_f = 2147483647.0f;
    }
    else if (scaled_f < -2147483647.0f)
    {
        scaled_f = -2147483647.0f;
    }

    int32_t scaled = (int32_t)((scaled_f >= 0.0f) ? (scaled_f + 0.5f) : (scaled_f - 0.5f));
    const char *sign = "";
    if (scaled < 0)
    {
        sign = "-";
        scaled = -scaled;
    }

    const int32_t whole = scaled / scale;
    const int32_t frac = scaled % scale;
    const char *suffix = (unit != NULL) ? unit : "";

    /* No printf float in the embedded renderer: newlib float formatting allocates heap. */
    if (decimals == 1U)
    {
        (void)snprintf(out, out_len, "%s%ld.%01ld%s", sign, (long)whole, (long)frac, suffix);
    }
    else
    {
        (void)snprintf(out, out_len, "%s%ld.%02ld%s", sign, (long)whole, (long)frac, suffix);
    }
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
            ui_renderer_template_format_fixed(value, 1U, desc->unit, out, out_len);
            break;

        case PARAM_DISPLAY_TIME_MS:
            ui_renderer_template_format_fixed(value * 1000.0f, 1U, "ms", out, out_len);
            break;

        case PARAM_DISPLAY_RATIO:
            ui_renderer_template_format_fixed(value, 2U, "", out, out_len);
            break;

        case PARAM_DISPLAY_INT:
            (void)snprintf(out, out_len, "%ld", (long)(value + 0.5f));
            break;

        default:
            ui_renderer_template_format_fixed(value, 2U, desc->unit, out, out_len);
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
        (void)snprintf(out, out_len, "--%%T---M---C0");
        return;
    }

    const uint32_t avg_permille = cpu_load_get_avg_permille();
    const uint32_t percent = (avg_permille + 5U) / 10U;
    (void)snprintf(out, out_len, "%lu%%", (unsigned long)percent);
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

static void ui_renderer_template_fit_text(char *txt, uint8_t max_px)
{
    if ((txt == NULL) || (max_px == 0U))
    {
        return;
    }

    if (drv_display_text_width(txt) <= max_px)
    {
        return;
    }

    uint32_t len = (uint32_t)strlen(txt);
    while ((len > 1U) && (drv_display_text_width(txt) > max_px))
    {
        if (len > 2U)
        {
            txt[len - 2U] = '.';
        }
        txt[len - 1U] = '\0';
        len--;
    }
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

static void ui_renderer_template_draw_brick_frame(int x, int y, int w, int h)
{
    ui_renderer_template_draw_open_corner_frame(x, y, w, h);

    if ((w > 8) && (h > 8))
    {
        drv_display_draw_pixel(x + 2, y + 2, true);
        drv_display_draw_pixel(x + w - 3, y + 2, true);
        drv_display_draw_pixel(x + 2, y + h - 3, true);
        drv_display_draw_pixel(x + w - 3, y + h - 3, true);
    }
}

static void ui_renderer_template_draw_param_frame(int x, int y, int w, int h)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

static void ui_renderer_template_draw_inverted_label(uint8_t x, uint8_t y, const char *txt, const font_t *font)
{
    drv_display_set_font(font);
    const uint8_t w = (uint8_t)(drv_display_text_width(txt) + 2U);
    const uint8_t h = (uint8_t)(drv_display_font_height() + 2U);
    drv_display_fill_rect(x, y, w, h);
    drv_display_draw_text_inverted((uint8_t)(x + 1U), (uint8_t)(y + 1U), txt);
}

static void ui_renderer_template_draw_track_badge(uint8_t x, uint8_t y, const char *txt)
{
    drv_display_set_font(&FONT_5X7);
    const uint8_t w = 11U;
    const uint8_t h = (uint8_t)(drv_display_font_height() + 2U);
    drv_display_fill_rect(x, y, w, h);
    drv_display_draw_text_inverted((uint8_t)(ui_renderer_template_center_x(x, w, txt) - 1), (uint8_t)(y + 1U), txt);
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

    return ui_param_get_active_track_display_value(id, ui_get_active_track());
}

static void ui_renderer_template_draw_param_slot(const ui_template_page_state_t *state,
                                                 const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                 uint8_t slot,
                                                 param_id_t id)
{
    const int x = g_ui_template_frame_x[slot];
    const int y = UI_TEMPLATE_FRAME_Y;
    const int widget_y = y + 1;
    const uint8_t slot_locked = ui_macro_interaction_param_is_locked(id);

    if (slot_locked != 0U)
    {
        drv_display_fill_rect(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);
        drv_display_set_draw_color(0U);
    }

    ui_renderer_template_draw_param_frame(x, y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);

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
                ui_renderer_template_fit_text(virt_name, UI_TEMPLATE_CARD_LABEL_MAX_PX);
                ui_renderer_template_fit_text(virt_value, UI_TEMPLATE_CARD_LABEL_MAX_PX);
                drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, virt_name),
                                      (uint8_t)(y + UI_TEMPLATE_CARD_LABEL_Y),
                                      virt_name);
                uiw_draw_enum_text(x, widget_y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, virt_value);
                drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, virt_value),
                                      (uint8_t)(y + UI_TEMPLATE_CARD_VALUE_Y),
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
    float macro_slot_scene_value = 0.0f;
    const uint8_t has_macro_slot_value =
        ui_macro_interaction_get_param_lock_value(id,
                                                  &macro_slot_track,
                                                  &macro_slot_scene_value);
    const uint8_t macro_value_visible =
        (uint8_t)((has_macro_slot_value != 0U)
                && (macro_slot_track == ui_get_active_track()));

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

    char name_txt[24];
    (void)snprintf(name_txt, sizeof(name_txt), "%s", desc->name);
    if ((state != NULL) && (state->param_text != NULL))
    {
        (void)state->param_text(slot,
                                id,
                                value,
                                name_txt,
                                (uint32_t)sizeof(name_txt),
                                value_txt,
                                (uint32_t)sizeof(value_txt));
    }

    drv_display_set_font(&FONT_4X6);
    ui_renderer_template_fit_text(name_txt, UI_TEMPLATE_CARD_LABEL_MAX_PX);
    ui_renderer_template_fit_text(value_txt, UI_TEMPLATE_CARD_LABEL_MAX_PX);
    if (slot_locked != 0U)
    {
        drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, name_txt),
                              (uint8_t)(y + UI_TEMPLATE_CARD_LABEL_Y),
                              name_txt);
    }
    else if (draw_name_inverted != 0U)
    {
        const uint8_t name_x = (uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, name_txt);
        drv_display_draw_text(name_x, (uint8_t)(y + UI_TEMPLATE_CARD_LABEL_Y), name_txt);
        drv_display_draw_line(name_x, y + 10, name_x + drv_display_text_width(name_txt) - 1, y + 10);
    }
    else
    {
        drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, name_txt),
                              (uint8_t)(y + UI_TEMPLATE_CARD_LABEL_Y),
                              name_txt);
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
            uiw_draw_switch(x, widget_y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, (value >= 0.5f) ? 1U : 0U);
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
            uiw_draw_enum_text(x, widget_y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, (enum_label != NULL) ? enum_label : value_txt);
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
            uiw_draw_jack_icon(x, widget_y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);
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
            uiw_draw_keyboard_icon(x, widget_y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H);
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
            uiw_draw_wave_icon(x, widget_y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, enum_label);
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
            uiw_draw_filter_icon(x, widget_y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, enum_label);
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
            uiw_draw_knob(x, widget_y, UI_TEMPLATE_FRAME_W, UI_TEMPLATE_FRAME_H, value, desc->min, desc->max);
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

    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, value_txt),
                          (uint8_t)(y + UI_TEMPLATE_CARD_VALUE_Y),
                          value_txt);

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
    char track_label[8];
    char runtime_label[12];
    char cpu_avg_label[40];
    char bpm_label[12];

    uint8_t role_u8 = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
    (void)track_runtime_get_voice_group_role(active_track, &role_u8);
    const uint8_t track_display_id = (uint8_t)(active_track + 1U);
    if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        (void)snprintf(track_label, sizeof(track_label), "M%u", (unsigned int)track_display_id);
    }
    else if (role_u8 == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        (void)snprintf(track_label, sizeof(track_label), "S%u", (unsigned int)track_display_id);
    }
    else
    {
        (void)snprintf(track_label, sizeof(track_label), "%u", (unsigned int)track_display_id);
    }
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

    const char *hall_mode_label = ui_get_hall_mode_short_label();
    const char *hall_mode_suffix = ui_get_hall_mode_suffix_label();

    ui_renderer_template_draw_brick_frame(0, 0, 15, 15);
    ui_renderer_template_draw_brick_frame(UI_TEMPLATE_HEADER_TITLE_X, 0, UI_TEMPLATE_HEADER_TITLE_W, 15);

    ui_renderer_template_draw_track_badge(2U, 2U, track_label);

    drv_display_set_font(&FONT_4X6);
    char runtime_fit[12];
    char hall_fit[12];
    (void)snprintf(runtime_fit, sizeof(runtime_fit), "%s", runtime_label);
    (void)snprintf(hall_fit, sizeof(hall_fit), "%s", hall_mode_label);
    ui_renderer_template_fit_text(runtime_fit, 25U);
    ui_renderer_template_fit_text(hall_fit, 20U);
    drv_display_draw_text(17U, 1U, runtime_fit);
    drv_display_draw_text(17U, 9U, hall_fit);
    if ((hall_mode_suffix != NULL) && (hall_mode_suffix[0] != '\0'))
    {
        char suffix_fit[8];
        (void)snprintf(suffix_fit, sizeof(suffix_fit), "%s", hall_mode_suffix);
        ui_renderer_template_fit_text(suffix_fit, 9U);
        drv_display_draw_text(33U, 9U, suffix_fit);
    }

    char title_fit[16];
    (void)snprintf(title_fit, sizeof(title_fit), "%s", family_title);
    drv_display_set_font(&FONT_5X7);
    if (drv_display_text_width(title_fit) > 38U)
    {
        drv_display_set_font(&FONT_4X6);
    }
    ui_renderer_template_fit_text(title_fit, 38U);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(UI_TEMPLATE_HEADER_TITLE_X,
                                                                  UI_TEMPLATE_HEADER_TITLE_W,
                                                                  title_fit),
                          4U,
                          title_fit);

    drv_display_set_font(&FONT_4X6);
    ui_renderer_template_fit_text(cpu_avg_label, 12U);
    if (draw_bpm != 0U)
    {
        if (bpm_inverted != 0U)
        {
            ui_renderer_template_draw_inverted_label(98U, 1U, bpm_label, &FONT_5X7);
        }
        else
        {
            drv_display_draw_text(98U, 1U, bpm_label);
        }
    }
    char pattern_label[6];
    ui_renderer_template_format_active_pattern_label(pattern_label, sizeof(pattern_label));
    const uint8_t cpu_x = (uint8_t)(104U - drv_display_text_width(cpu_avg_label) - 6U);
    drv_display_draw_text(cpu_x, 9U, cpu_avg_label);
    drv_display_draw_text(104U, 9U, pattern_label);
}

static void ui_renderer_template_draw_footer(const ui_template_page_state_t *state)
{
    const ui_template_family_t *family = ui_template_page_get_active_family(state);

    drv_display_set_font(&FONT_4X6);

    for (uint8_t i = 0U; i < 4U; i++)
    {
        const int bx = g_ui_template_footer_x[i];
        const int bw = g_ui_template_footer_w[i];
        const uint8_t subpage_selectable = ui_template_page_is_subpage_selectable(state, i);
        const char *label = family->nav_labels[i];

        if (subpage_selectable == 0U)
        {
            label = "-";
        }

        if ((label == NULL) || (label[0] == '\0'))
        {
            label = "-";
        }


        char label_fit[12];
        (void)snprintf(label_fit, sizeof(label_fit), "%s", label);
        ui_renderer_template_fit_text(label_fit, 28U);

        const int x_label = ui_renderer_template_center_x(bx, bw, label_fit);

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

            drv_display_draw_text_inverted((uint8_t)x_label, UI_TEMPLATE_FOOTER_TEXT_Y, label_fit);
        }
        else
        {
            ui_renderer_template_draw_brick_frame(bx, UI_TEMPLATE_FOOTER_Y, bw, UI_TEMPLATE_FOOTER_H);
            drv_display_draw_text((uint8_t)x_label, UI_TEMPLATE_FOOTER_TEXT_Y, label_fit);
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

    const ui_template_subpage_t *subpage = ui_template_page_get_active_subpage(state);
    if (subpage != NULL)
    {
        for (uint8_t i = 0U; i < 4U; i++)
        {
            ui_renderer_template_draw_param_slot(state, &plock_frame_ctx, i, subpage->param_bank.params[i]);
        }
    }

    ui_renderer_template_draw_footer(state);

    drv_display_set_font(&FONT_5X7);
}
