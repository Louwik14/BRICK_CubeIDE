#include "ui_renderer_template.h"

#include <stdio.h>
#include <string.h>

#include "cpu_load.h"
#include "drv_display.h"
#include "font.h"
#include "mixer.h"
#include "param_registry.h"
#include "ui_core.h"
#include "ui_macro_interaction.h"
#include "ui_param.h"
#include "ui_widgets.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Storage/project_v1.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Mod/mod_lfo_v1.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sampler_ram_pool.h"

#define UI_TEMPLATE_FRAME_W          32
#define UI_TEMPLATE_FRAME_H          38
#define UI_TEMPLATE_FRAME_Y          17
#define UI_TEMPLATE_FOOTER_Y         55
#define UI_TEMPLATE_FOOTER_H         9
#define UI_TEMPLATE_FOOTER_TEXT_Y    57
#define UI_TEMPLATE_CARD_TEXT_Y      (UI_TEMPLATE_FRAME_H - 7)
#define UI_TEMPLATE_CARD_LABEL_Y     UI_TEMPLATE_CARD_TEXT_Y
#define UI_TEMPLATE_CARD_LABEL_H     7
#define UI_TEMPLATE_CARD_WIDGET_X_PAD 1
#define UI_TEMPLATE_CARD_WIDGET_Y    1
#define UI_TEMPLATE_CARD_WIDGET_W    (UI_TEMPLATE_FRAME_W - (2 * UI_TEMPLATE_CARD_WIDGET_X_PAD))
#define UI_TEMPLATE_CARD_WIDGET_H    (UI_TEMPLATE_CARD_LABEL_Y - UI_TEMPLATE_CARD_WIDGET_Y - 1)
#define UI_TEMPLATE_GROUP_WIDGET_X   1
#define UI_TEMPLATE_GROUP_WIDGET_Y   (UI_TEMPLATE_FRAME_Y + UI_TEMPLATE_CARD_WIDGET_Y)
#define UI_TEMPLATE_GROUP_WIDGET_W   126
#define UI_TEMPLATE_GROUP_WIDGET_H   UI_TEMPLATE_CARD_WIDGET_H
#define UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST 1U
#define UI_TEMPLATE_FILTER_GROUP_SLOT_COUNT 2U
#define UI_TEMPLATE_CARD_LABEL_MAX_PX 28U
#define UI_TEMPLATE_HEADER_TITLE_X   43
#define UI_TEMPLATE_HEADER_TITLE_W   42
#define UI_TEMPLATE_SAMPLER_NAME_Y   17
#define UI_TEMPLATE_SAMPLER_WAVE_X   1
#define UI_TEMPLATE_SAMPLER_WAVE_Y   25
#define UI_TEMPLATE_SAMPLER_WAVE_W   126
#define UI_TEMPLATE_SAMPLER_WAVE_H   17
#define UI_TEMPLATE_SAMPLER_LABEL_Y  (UI_TEMPLATE_FRAME_Y + UI_TEMPLATE_CARD_LABEL_Y)
#define UI_TEMPLATE_SAMPLER_TEXT_MAX_PX UI_TEMPLATE_CARD_LABEL_MAX_PX
#define UI_TEMPLATE_SAMPLER_WAVE_INNER_W (UI_TEMPLATE_SAMPLER_WAVE_W - 2)
#define UI_TEMPLATE_SAMPLER_WAVE_INNER_H (UI_TEMPLATE_SAMPLER_WAVE_H - 2)

typedef struct
{
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release;
    uint8_t locked[4];
} ui_renderer_template_adsr_shape_t;

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

void ui_format_param_127_00(float value, float min_value, float max_value, char *out, uint32_t out_len)
{
    float normalized = 0.0f;
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    if (max_value > min_value)
    {
        if (value < min_value)
        {
            value = min_value;
        }
        else if (value > max_value)
        {
            value = max_value;
        }
        normalized = ((value - min_value) * 127.0f) / (max_value - min_value);
    }

    ui_renderer_template_format_fixed(normalized, 2U, "", out, out_len);
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
            ui_format_param_127_00(value, desc->min, desc->max, out, out_len);
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

static uint8_t ui_renderer_template_right_x(uint8_t right_margin_px, uint8_t text_w)
{
    if (text_w >= (uint8_t)(OLED_WIDTH - right_margin_px))
    {
        return 0U;
    }

    return (uint8_t)(OLED_WIDTH - right_margin_px - text_w);
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

static uint8_t ui_renderer_template_get_visible_param_value(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                            param_id_t id,
                                                            float *out_value,
                                                            uint8_t *out_inverted)
{
    if ((id >= PARAM_COUNT) || (out_value == 0))
    {
        return 0U;
    }

    if (out_inverted != 0)
    {
        *out_inverted = 0U;
    }

    float value = ui_renderer_template_get_param_display_value(id);
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
        (void)ui_param_try_get_seq_plock_feedback_with_frame(plock_frame_ctx, id, &value, out_inverted);
    }

    *out_value = value;
    return 1U;
}

static void ui_renderer_template_build_param_text(const ui_template_page_state_t *state,
                                                  uint8_t slot,
                                                  param_id_t id,
                                                  float value,
                                                  char *name_txt,
                                                  uint32_t name_len,
                                                  char *value_txt,
                                                  uint32_t value_len)
{
    const param_desc_t *desc = &param_registry[id];

    if ((name_txt != NULL) && (name_len > 0U))
    {
        (void)snprintf(name_txt, name_len, "%s", desc->name);
    }
    if ((value_txt != NULL) && (value_len > 0U))
    {
        ui_renderer_template_format_value(id, value, value_txt, value_len);
    }

    if ((state != NULL) && (state->param_text != NULL))
    {
        (void)state->param_text(slot,
                                id,
                                value,
                                name_txt,
                                name_len,
                                value_txt,
                                value_len);
    }
}

static uint8_t ui_renderer_template_prepare_param_slot_texts(const ui_template_page_state_t *state,
                                                             const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                             uint8_t slot,
                                                             param_id_t id,
                                                             float *out_value,
                                                             uint8_t *out_draw_name_inverted,
                                                             char *name_txt,
                                                             uint32_t name_len,
                                                             char *value_txt,
                                                             uint32_t value_len,
                                                             char *bottom_txt,
                                                             uint32_t bottom_len,
                                                             uint8_t *out_flash_active)
{
    if ((id >= PARAM_COUNT) || (out_value == NULL) || (out_draw_name_inverted == NULL)
            || (name_txt == NULL) || (name_len == 0U)
            || (value_txt == NULL) || (value_len == 0U)
            || (bottom_txt == NULL) || (bottom_len == 0U))
    {
        return 0U;
    }

    *out_draw_name_inverted = 0U;
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, id, out_value, out_draw_name_inverted);

    ui_renderer_template_build_param_text(state,
                                          slot,
                                          id,
                                          *out_value,
                                          name_txt,
                                          name_len,
                                          value_txt,
                                          value_len);

    float flash_value = 0.0f;
    ui_param_value_flash_kind_t flash_kind = UI_PARAM_VALUE_FLASH_DIRECT;
    const uint8_t flash_active =
        ui_param_get_slot_value_flash(slot, id, ui_get_active_track(), &flash_value, &flash_kind);
    if (out_flash_active != NULL)
    {
        *out_flash_active = flash_active;
    }
    if (flash_active != 0U)
    {
        char flash_name[24];
        (void)flash_kind;
        ui_renderer_template_build_param_text(state,
                                              slot,
                                              id,
                                              flash_value,
                                              flash_name,
                                              (uint32_t)sizeof(flash_name),
                                              bottom_txt,
                                              bottom_len);
    }
    else
    {
        (void)snprintf(bottom_txt, bottom_len, "%s", name_txt);
    }

    return 1U;
}

static int ui_renderer_template_clamp_i32(int value, int min_value, int max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static uint8_t ui_renderer_template_value_to_u7(float value)
{
    int32_t v = (int32_t)(value + 0.5f);
    if (v < 0)
    {
        v = 0;
    }
    if (v > 127)
    {
        v = 127;
    }
    return (uint8_t)v;
}

static uint8_t ui_renderer_template_filter_type_visible(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                        mixer_track_filter_type_t *out_type)
{
    float value = 0.0f;
    if ((out_type == 0)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_FILTER_TYPE, &value, 0) == 0U))
    {
        return 0U;
    }

    uint8_t type = (uint8_t)(value + 0.5f);
    if (type > (uint8_t)MIXER_TRACK_FILTER_BP_BI)
    {
        type = (uint8_t)MIXER_TRACK_FILTER_BP_BI;
    }
    *out_type = (mixer_track_filter_type_t)type;
    return 1U;
}

static uint8_t ui_renderer_template_filter_param_supported(uint8_t active_track, param_id_t id)
{
    const track_runtime_param_status_t status = track_runtime_get_effective_param_status(active_track, id);
    return (uint8_t)((status == TRACK_RUNTIME_PARAM_ALLOWED) || (status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED));
}

static uint8_t ui_renderer_template_draw_filter_text(int x, int y, int w, int h, const char *txt, const font_t *font, int y_offset)
{
    if ((txt == 0) || (font == 0) || (w <= 0) || (h <= 0))
    {
        return 0U;
    }

    drv_display_set_font(font);
    const int text_w = (int)drv_display_text_width(txt);
    const int text_h = (int)drv_display_font_height();
    int text_x = x + ((w - text_w) / 2);
    int text_y = y + ((h - text_h) / 2) + y_offset;
    if (text_x < x)
    {
        text_x = x;
    }
    if (text_y < y)
    {
        text_y = y;
    }
    drv_display_draw_text((uint8_t)text_x, (uint8_t)text_y, txt);
    drv_display_set_font(&FONT_4X6);
    return 1U;
}

static uint8_t ui_renderer_template_draw_filter_big_text(int x, int y, int w, int h, const char *txt, const font_t *font)
{
    return ui_renderer_template_draw_filter_text(x, y, w, h, txt, font, 4);
}

static uint8_t ui_renderer_template_draw_lfo_dest_text(int x, int y, int w, int h, float value)
{
    if ((w <= 0) || (h <= 0))
    {
        return 0U;
    }

    char label[20];
    if (mod_lfo_v1_dest_short_label(ui_get_active_track(), (uint16_t)(value + 0.5f), label, (uint32_t)sizeof(label)) == 0U)
    {
        (void)snprintf(label, sizeof(label), "-");
    }

    drv_display_set_font(&FONT_OFF_COMPACT);
    ui_renderer_template_fit_text(label, (uint8_t)((w > 2) ? (w - 2) : w));
    return ui_renderer_template_draw_filter_text(x, y, w, h, label, &FONT_OFF_COMPACT, 4);
}

static uint8_t ui_renderer_template_draw_play_note_text(param_id_t id, int x, int y, int w, int h, float value)
{
    if ((w <= 0) || (h <= 0))
    {
        return 0U;
    }

    char label[8];
    ui_renderer_template_format_value(id, value, label, (uint32_t)sizeof(label));

    const font_t *font = &FONT_HELVB14;
    drv_display_set_font(font);
    if (drv_display_text_width(label) > (uint8_t)((w > 2) ? (w - 2) : w))
    {
        font = &FONT_OFF_COMPACT;
        drv_display_set_font(font);
        ui_renderer_template_fit_text(label, (uint8_t)((w > 2) ? (w - 2) : w));
    }

    return ui_renderer_template_draw_filter_text(x, y, w, h, label, font, 4);
}

static void ui_renderer_template_draw_point(int x, int y)
{
    drv_display_draw_line(x, y, x, y);
}

static void ui_renderer_template_draw_circle_points(int cx, int cy, int x, int y)
{
    ui_renderer_template_draw_point(cx + x, cy + y);
    ui_renderer_template_draw_point(cx - x, cy + y);
    ui_renderer_template_draw_point(cx + x, cy - y);
    ui_renderer_template_draw_point(cx - x, cy - y);
    ui_renderer_template_draw_point(cx + y, cy + x);
    ui_renderer_template_draw_point(cx - y, cy + x);
    ui_renderer_template_draw_point(cx + y, cy - x);
    ui_renderer_template_draw_point(cx - y, cy - x);
}

static void ui_renderer_template_draw_circle_outline(int cx, int cy, int radius)
{
    int px = 0;
    int py = radius;
    int d = 1 - radius;

    while (px <= py)
    {
        ui_renderer_template_draw_circle_points(cx, cy, px, py);
        px++;
        if (d < 0)
        {
            d += (2 * px) + 1;
        }
        else
        {
            py--;
            d += (2 * (px - py)) + 1;
        }
    }
}

static void ui_renderer_template_draw_cfg_piano_icon(int x, int y, int w, int h)
{
    const int key_w = 26;
    const int key_h = 15;
    const int key_x = x + ((w - key_w) / 2);
    const int key_y = y + ((h - key_h) / 2) + 2;
    static const int8_t div_x[] = {4, 8, 12, 15, 19, 23};
    static const int8_t black_x[] = {3, 7, 14, 18, 22};

    drv_display_draw_rect(key_x, key_y, key_w, key_h);
    for (uint8_t i = 0U; i < 6U; ++i)
    {
        drv_display_draw_line(key_x + div_x[i], key_y + 7, key_x + div_x[i], key_y + key_h - 1);
    }
    for (uint8_t i = 0U; i < 5U; ++i)
    {
        drv_display_fill_rect(key_x + black_x[i], key_y + 1, 3, 7);
    }
}

static void ui_renderer_template_draw_cfg_input_icon(int x, int y, int w, int h)
{
    static const char *const input[29] = {
        "WWWWWWWWWWWWWBBBWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWBWWWBWWWWWWWWWWWWW",
        "WWWWWWWWWWWWBWWWBWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWBBBWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWBWWWBWWWWWWWWWWWWW",
        "WWWWWWWWWWWWBBBBBWWWWWWWWWWWWW",
        "WWWWWWWWWWWWBWWWBWWWWWWWWWWWWW",
        "WWWWWWWWWWWWBWWWBWWWWWWWWWWWWW",
        "WWWWWWWWWWWWBBBBBWWWWWWWWWWWWW",
        "WWWWWWWWWWWWBWWWBWWWWWWWWWWWWW",
        "WWWWWWWWWWWWBWWWBWWWWWWWWWWWWW",
        "WWWWWWWWWWWWBWWWBWWWWWWWWWWWWW",
        "WWWWWWWWWWWBBBBBBBWWWWWWWWWWWW",
        "WWWWWWWWWWWBWWWWWBWWWWWWWWWWWW",
        "WWWWWWWWWWWBWWWWWBWWWWWWWWWWWW",
        "WWWWWWWWWWBBBBBBBBBWWWWWWWWWWW",
        "WWWWWWWWWWBWWWWWWWBWWWWWWWWWWW",
        "WWWWWWWWWWBWWWWWWWBWWWWWWWWWWW",
        "WWWWWWWWWWBWWWWWBWBWWWWWWWWWWW",
        "WWWWWWWWWWBWWWWWBWBWWWWWWWWWWW",
        "WWWWWWWWWWBWWWWWBWBWWWWWWWWWWW",
        "WWWWWWWWWWBWWWWWBWBWWWWWWWWWWW",
        "WWWWWWWWWWBWWWWWBWBWWWWWWWWWWW",
        "WWWWWWWWWWBWWWWBWWBWWWWWWWWWWW",
        "WWWWWWWWWWBWWWBWWWBWWWWWWWWWWW",
        "WWWWWWWWWWBBWWWWWBBWWWWWWWWWWW",
        "WWWWWWWWWWWBBWWWBBWWWWWWWWWWWW",
        "WWWWWWWWWWWWBBBBBWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWBBBWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2);
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (input[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_hybrid_icon(int x, int y, int w, int h)
{
    static const char *const hybrid[29] = {
        "WWWWBBBWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBWWWBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBWWWBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWBBBWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBWWWBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBBBBBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBWWWBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBWWWBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWBBBBBWWWWWWWWWBBBBBBBBBBBWW",
        "WWWBWWWBWWWWWWWWWBWBBBWBBBWBWW",
        "WWWBWWWBWWWWWWWWWBWBBBWBBBWBWW",
        "WWWBWWWBWWWWWWWWWBWBBBWBBBWBWW",
        "WWBBBBBBBWWWWBWWWBWBBBWBBBWBWW",
        "WWBWWWWWBWWWWBWWWBWBBBWBBBWBWW",
        "WWBWWWWWBWWBBBBBWBWWBWWWBWWBWW",
        "WBBBBBBBBBWWWBWWWBWWBWWWBWWBWW",
        "WBWWWWWWWBWWWBWWWBWWBWWWBWWBWW",
        "WBWWWWWWWBWWWWWWWBWWBWWWBWWBWW",
        "WBWWWWWBWBWWWWWWWBWWBWWWBWWBWW",
        "WBWWWWWBWBWWWWWWWBWWBWWWBWWBWW",
        "WBWWWWWBWBWWWWWWWBWWBWWWBWWBWW",
        "WBWWWWWBWBWWWWWWWBBBBBBBBBBBWW",
        "WBWWWWWBWBWWWWWWWWWWWWWWWWWWWW",
        "WBWWWWBWWBWWWWWWWWWWWWWWWWWWWW",
        "WBWWWBWWWBWWWWWWWWWWWWWWWWWWWW",
        "WBBWWWWWBBWWWWWWWWWWWWWWWWWWWW",
        "WWBBWWWBBWWWWWWWWWWWWWWWWWWWWW",
        "WWWBBBBBWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWBBBWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2);
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (hybrid[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static uint8_t ui_renderer_template_draw_cfg_midi_text(int x, int y, int w, int h)
{
    static const char label[] = "Midi";

    drv_display_set_font(&FONT_OFF_COMPACT);
    const int text_w = (int)drv_display_text_width(label);
    const int text_h = (int)drv_display_font_height();
    int text_x = x + ((w - text_w) / 2);
    int text_y = y + ((h - text_h) / 2);

    if (text_x < x)
    {
        text_x = x;
    }
    if (text_y < y)
    {
        text_y = y;
    }

    drv_display_draw_text((uint8_t)text_x, (uint8_t)text_y, label);
    drv_display_set_font(&FONT_4X6);
    return 1U;
}

static void ui_renderer_template_draw_cfg_midi_icon(int x, int y, int w, int h)
{
    static const char *const midi[29] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWBBBBBBBBBBBWWWWWWWWWW",
        "WWWWWWWWWBWBBBWBBBWBWWWWWWWWWW",
        "WWWWWWWWWBWBBBWBBBWBWWWWWWWWWW",
        "WWWWWWWWWBWBBBWBBBWBWWWWWWWWWW",
        "WWWWWWWWWBWBBBWBBBWBWWWWWWWWWW",
        "WWWWWWWWWBWBBBWBBBWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBWWBWWWBWWBWWWWWWWWWW",
        "WWWWWWWWWBBBBBBBBBBBWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2);
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (midi[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_synth_icon(int x, int y, int w, int h)
{
    static const char *const synth[29] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
        "BWWWWWWBWWWBWWWWWWWWWWWWWWWWWB",
        "BWBBBBWBBBBBWBWBWBWBWBWBWBWBWB",
        "BWWWWWWBWWWBWWWWWWWWWWWWWWWWWB",
        "BWBWBBWBBBBBWBWBWBWBWBWBWBWBWB",
        "BWBWBBWBWWWBWWWWWWWWWWWWWWWWWB",
        "BWWWWWWBWWWBWWWWWWWWWWWWWWWWWB",
        "BWWWWWWBWWWBWWWWWWWWWWWWWWWWWB",
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
        "BWWWWWWWWWWWWWWWWWWWWWWWWWWWWB",
        "BWWBWBWWWBWBWBWWWBWBWWWBWBWBWB",
        "BWWBWBWWWBWBWBWWWBWBWWWBWBWBWB",
        "BWWWWWWWWWWWWWWWWWWWWWWWWWWWWB",
        "BWBBWBBWBBWBBWBBWBBWBBWBBWBBWB",
        "BWBBWBBWBBWBBWBBWBBWBBWBBWBBWB",
        "BWBBWBBWBBWBBWBBWBBWBBWBBWBBWB",
        "WBBBBBBBBBBBBBBBBBBBBBBBBBBBBW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2);
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (synth[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_wave_icon(int x, int y, int w, int h)
{
    static const char *const wave[29] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWBWBWWBWWBWBWBBBWWWWWWWW",
        "WWWWWWWBWBWBWBWBWBWBWWWWWWWWWW",
        "WWWWWWWBWBWBBBWBWBWBBBWWWWWWWW",
        "WWWWWWWBBBWBWBWBWBWBWWWWWWWWWW",
        "WWWWWWWBWBWBWBWWBWWBBBWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWBWWWWWWWBWWWWWWWBWWWWW",
        "WWWWWWBBWBWWWWBBWBWWWWBBWWWWWW",
        "WWWWWBWWWWBBWBWWWWBBWBWWWWWWWW",
        "WWWWBWWWWBWWBWWWWBWWBWWWWBWWWW",
        "WWWWWWWBBWBWWWWBBWBWWWWBBWWWWW",
        "WWWWWWBWWWWBBWBWWWWBBWBWWWWWWW",
        "WWWWWBWWWWWWWBWWWWWWWBWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2);
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (wave[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_drum_icon(int x, int y, int w, int h)
{
    const int cx = x + (w / 2);
    const int top = y + 12;
    const int left = cx - 10;
    const int right = cx + 10;
    const int bottom = top + 11;

    drv_display_draw_line(cx - 11, y + 4, cx - 2, y + 10);
    drv_display_draw_line(cx + 11, y + 4, cx + 2, y + 10);
    drv_display_draw_line(cx - 12, y + 4, cx - 10, y + 4);
    drv_display_draw_line(cx + 10, y + 4, cx + 12, y + 4);
    drv_display_draw_line(left + 2, top, right - 2, top);
    drv_display_draw_line(left, top + 2, left, bottom - 3);
    drv_display_draw_line(right, top + 2, right, bottom - 3);
    drv_display_draw_line(left + 2, bottom, right - 2, bottom);
    drv_display_draw_line(left, top + 2, left + 2, top);
    drv_display_draw_line(right - 2, top, right, top + 2);
    drv_display_draw_line(left, bottom - 3, left + 2, bottom);
    drv_display_draw_line(right - 2, bottom, right, bottom - 3);
    drv_display_draw_line(left + 1, top + 5, right - 1, top + 5);
}

static void ui_renderer_template_draw_cfg_sampler_icon(int x, int y, int w, int h)
{
    static const char *const vinyl[29] = {
        "..............................",
        "..............................",
        "..............................",
        "..............................",
        "............#####.............",
        "..........#########...........",
        "........#############.........",
        ".......###########W###........",
        ".......############W##........",
        "......###########W##W##.......",
        "......############W#W##.......",
        ".....#############W###W#......",
        ".....########WWW######W#......",
        ".....########W#W######W#......",
        ".....########WWW#####W##......",
        ".....###W#W#########W###......",
        "......##W#W########W###.......",
        "......##W##W######W####.......",
        ".......##W####WWWW####........",
        ".......###W###WWW#####........",
        "........#############.........",
        "..........#########...........",
        "............#####.............",
        "..............................",
        "..............................",
        "..............................",
        "..............................",
        "..............................",
        "..............................",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2) ;
    const int icon_y = y + ((h - icon_h) / 2) + 1;

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (vinyl[row][col] == '#')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_stream_icon(int x, int y, int w, int h)
{
    static const char *const stream[29] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWBBBBBBBBBBBBBBBBWWWWWWWWWWWW",
        "WBBWWWWWWWWWWWWWWBBWWWWWWWWWWW",
        "WBBWWWBBWBBWBBWBBWBBWWWWWWWWWW",
        "WBBWWWBBWBBWBBWBBWWBBWWWWWWWWW",
        "WBBWWWBBWBBWBBWBBWWWBBWWWWWWWW",
        "WBBWWWBBWBBWBBWBBWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWBBWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWBBBWWWWWWWWWBBWWWWWWWW",
        "WBBWWWWWBBBBBWWWWWWWBWWWWWWWWW",
        "WBBWWWWWBBBBBBBWWWWWBWWWWWWWWW",
        "WBBWWWWWBBBBBBBBWWWWBBWWWWWWWW",
        "WBBWWWWWBBBBBBBBBWWWWBWWWWWWWW",
        "WBBWWWWWBBBBBBBBWWWWWBWWWWWWWW",
        "WBBWWWWWBBBBBBBWWWWWWBWWWWWWWW",
        "WBBWWWWWBBBBBWWWWWWWWBWWWWWWWW",
        "WBBWWWWWBBBWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWBBWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBWWWWWWWWWWWWWWWWWWBWWWWWWWW",
        "WBBBBBBBBBBBBBBBBBBBBBWWWWWWWW",
        "WWBBBBBBBBBBBBBBBBBBBWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2) + 4;
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (stream[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_ram_icon(int x, int y, int w, int h)
{
    static const char *const ram[29] = {
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWBBWWBBWWBBWWBBWWBBWWWWWWW",
        "WWWWWBBWWBBWWBBWWBBWWBBWWWWWWW",
        "WWWWBBBBBBBBBBBBBBBBBBBBBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBWWBBBWWWBBWWBWWWBWWBWWWWW",
        "WWWWBWWBWWBWBWWBWBBWBBWWBBBWWW",
        "WWBBBWWBBBWWBBBBWBWBWBWWBBBWWW",
        "WWBBBWWBWBWWBWWBWBWWWBWWBWWWWW",
        "WWWWBWWBWWBWBWWBWBWWWBWWBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBBBWWW",
        "WWBBBWWWWWWWWWWWWWWWWWWWBWWWWW",
        "WWWWBBBBBBBBBBBBBBBBBBBBBWWWWW",
        "WWWWWWBBWWBBWWBBWWBBWWBBWWWWWW",
        "WWWWWWBBWWBBWWBBWWBBWWBBWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
        "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
    };
    const int icon_w = 30;
    const int icon_h = 29;
    const int icon_x = x + ((w - icon_w) / 2);
    const int icon_y = y + ((h - icon_h) / 2);

    for (uint8_t row = 0U; row < 29U; ++row)
    {
        for (uint8_t col = 0U; col < 30U; ++col)
        {
            if (ram[row][col] == 'B')
            {
                drv_display_draw_line(icon_x + (int)col, icon_y + (int)row, icon_x + (int)col, icon_y + (int)row);
            }
        }
    }
}

static void ui_renderer_template_draw_cfg_loop_icon(int x, int y, int w, int h)
{
    const int bx = x + 4;
    const int by = y + 6;
    const int bw = w - 8;
    const int bh = h - 10;
    const int r1x = bx + 7;
    const int r2x = bx + bw - 8;
    const int ry = by + 9;

    drv_display_draw_rect(bx, by, bw, bh);
    drv_display_draw_line(bx + 3, by + 3, bx + bw - 4, by + 3);
    ui_renderer_template_draw_circle_outline(r1x, ry, 3);
    ui_renderer_template_draw_circle_outline(r2x, ry, 3);
    drv_display_draw_line(r1x + 3, ry, r2x - 3, ry);
    drv_display_draw_line(bx + 5, by + bh - 4, bx + bw - 6, by + bh - 4);
    drv_display_draw_line(bx + 8, by + bh - 7, bx + bw - 9, by + bh - 7);
}

static uint8_t ui_renderer_template_draw_cfg_stacked_text(int x,
                                                          int y,
                                                          int w,
                                                          int h,
                                                          const char *top,
                                                          const char *bottom)
{
    if ((top == 0) || (bottom == 0))
    {
        return 0U;
    }

    drv_display_set_font(&FONT_4X6);
    const int font_h = (int)drv_display_font_height();
    const int gap = 1;
    int text_y = y + ((h - ((2 * font_h) + gap)) / 2);
    if (text_y < y)
    {
        text_y = y;
    }
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, w, top), (uint8_t)text_y, top);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, w, bottom), (uint8_t)(text_y + font_h + gap), bottom);
    return 1U;
}

static ui_track_family_t ui_renderer_template_cfg_visible_family(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx)
{
    float family_value = 0.0f;
    if (ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_CFG_TRACK, &family_value, 0) == 0U)
    {
        return ui_get_track_family(ui_get_active_track());
    }

    int32_t family = (int32_t)(family_value + 0.5f);
    if (family < 0)
    {
        family = 0;
    }
    if (family >= (int32_t)UI_TRACK_FAMILY_COUNT)
    {
        family = (int32_t)UI_TRACK_FAMILY_COUNT - 1;
    }
    return (ui_track_family_t)family;
}

static uint8_t ui_renderer_template_cfg_input_index(ui_track_family_t family)
{
    if ((family >= UI_TRACK_FAMILY_INPUT1) && (family <= UI_TRACK_FAMILY_INPUT4))
    {
        return (uint8_t)((uint8_t)family - (uint8_t)UI_TRACK_FAMILY_INPUT1 + 1U);
    }
    return 0U;
}

static uint8_t ui_renderer_template_draw_custom_track_cfg(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                          ui_template_custom_widget_kind_t kind,
                                                          int x,
                                                          int y,
                                                          int w,
                                                          int h,
                                                          float value)
{
    if ((w <= 0) || (h <= 0))
    {
        return 0U;
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_INACTIVE)
    {
        return ui_renderer_template_draw_filter_text(x, y, w, h, "-", &FONT_4X6, 0);
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_CHANNEL)
    {
        uint8_t channel = (uint8_t)(value + 0.5f);
        if (channel < 1U)
        {
            channel = 1U;
        }
        if (channel > 16U)
        {
            channel = 16U;
        }

        char label[4];
        (void)snprintf(label, sizeof(label), "%u", (unsigned int)channel);
        return ui_renderer_template_draw_filter_big_text(x, y, w, h, label, &FONT_HELVB14);
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_SOURCE)
    {
        static const char *const labels[] = {"Int", "Ext", "All"};
        uint8_t source = (uint8_t)(value + 0.5f);
        if (source >= 3U)
        {
            source = 2U;
        }
        return ui_renderer_template_draw_filter_big_text(x, y, w, h, labels[source], &FONT_OFF_COMPACT);
    }

    const ui_track_family_t family = ui_renderer_template_cfg_visible_family(plock_frame_ctx);
    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TRACK)
    {
        const uint8_t input_index = ui_renderer_template_cfg_input_index(family);
        if (family == UI_TRACK_FAMILY_OFF)
        {
            return ui_renderer_template_draw_filter_big_text(x, y, w, h, "OFF", &FONT_OFF_COMPACT);
        }
        if (input_index != 0U)
        {
            char label[6];
            (void)snprintf(label, sizeof(label), "In%u", (unsigned int)input_index);
            return ui_renderer_template_draw_filter_big_text(x, y, w, h, label, &FONT_OFF_COMPACT);
        }
        if (family == UI_TRACK_FAMILY_SYNTH)
        {
            ui_renderer_template_draw_cfg_synth_icon(x, y, w, h);
            return 1U;
        }
        if (family == UI_TRACK_FAMILY_MIDI)
        {
            return ui_renderer_template_draw_cfg_midi_text(x, y, w, h);
        }
        if (family == UI_TRACK_FAMILY_DRUM)
        {
            ui_renderer_template_draw_cfg_drum_icon(x, y, w, h);
            return 1U;
        }
        if (family == UI_TRACK_FAMILY_SAMPLER)
        {
            ui_renderer_template_draw_cfg_sampler_icon(x, y, w, h);
            return 1U;
        }
        return ui_renderer_template_draw_filter_big_text(x, y, w, h, ui_get_track_family_display_name(family), &FONT_OFF_COMPACT);
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TYPE)
    {
        const ui_track_type_t type = ui_get_track_type_from_family_index(family, (uint8_t)(value + 0.5f));
        if (family == UI_TRACK_FAMILY_OFF)
        {
            return ui_renderer_template_draw_filter_text(x, y, w, h, "-", &FONT_4X6, 0);
        }

        switch (type)
        {
            case UI_TRACK_TYPE_WAVE:
                ui_renderer_template_draw_cfg_wave_icon(x, y, w, h);
                return 1U;

            case UI_TRACK_TYPE_MULTI:
                ui_renderer_template_draw_cfg_piano_icon(x, y, w, h);
                return 1U;

            case UI_TRACK_TYPE_MIDI:
                ui_renderer_template_draw_cfg_midi_icon(x, y, w, h);
                return 1U;

            case UI_TRACK_TYPE_AUDIO:
            {
                const uint8_t input_index = ui_renderer_template_cfg_input_index(family);
                if (input_index != 0U)
                {
                    ui_renderer_template_draw_cfg_input_icon(x, y, w, h);
                    return 1U;
                }
                break;
            }

            case UI_TRACK_TYPE_HYBRID:
                ui_renderer_template_draw_cfg_hybrid_icon(x, y, w, h);
                return 1U;

            case UI_TRACK_TYPE_SAMPLER:
                ui_renderer_template_draw_cfg_ram_icon(x, y, w, h);
                return 1U;

            case UI_TRACK_TYPE_CLIP:
                ui_renderer_template_draw_cfg_stream_icon(x, y, w, h);
                return 1U;

            case UI_TRACK_TYPE_LOOPER:
                ui_renderer_template_draw_cfg_loop_icon(x, y, w, h);
                return 1U;

            case UI_TRACK_TYPE_DRUM_TRX_BD:
                return ui_renderer_template_draw_cfg_stacked_text(x, y, w, h, "TRX", "BD");

            case UI_TRACK_TYPE_DRUM_BD_ANALOG:
                return ui_renderer_template_draw_cfg_stacked_text(x, y, w, h, "BD", "Ana");

            case UI_TRACK_TYPE_MASTER_FX:
                return ui_renderer_template_draw_filter_big_text(x, y, w, h, "FX", &FONT_OFF_COMPACT);

            default:
                break;
        }

        return ui_renderer_template_draw_filter_big_text(x, y, w, h, ui_get_track_type_short_name(family, type), &FONT_OFF_COMPACT);
    }

    return 0U;
}

static const char *ui_renderer_template_filter_type_short_label(mixer_track_filter_type_t filter_type)
{
    switch (filter_type)
    {
        case MIXER_TRACK_FILTER_OFF:
            return "OFF";
        case MIXER_TRACK_FILTER_EQ3:
            return "DJ";
        case MIXER_TRACK_FILTER_LP_BI:
            return "LP";
        case MIXER_TRACK_FILTER_HP_BI:
            return "HP";
        case MIXER_TRACK_FILTER_BP_BI:
            return "BP";
        default:
            return NULL;
    }
}

static void ui_renderer_template_draw_filter_curve_segment(int x0, int y0, int x1, int y1, int baseline_y)
{
    const int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    const int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);

    if ((dx == 0) && (dy == 0))
    {
        return;
    }
    if ((dy == 0) && (dx <= 1) && (y0 >= (baseline_y - 2)))
    {
        return;
    }

    drv_display_draw_line(x0, y0, x1, y1);
}

static uint8_t ui_renderer_template_draw_filter_group_curve(mixer_track_filter_type_t filter_type,
                                                            float cutoff_value,
                                                            float resonance_value,
                                                            int x,
                                                            int y,
                                                            int w,
                                                            int h)
{
    if ((w < 24) || (h < 10))
    {
        return 0U;
    }

    const uint8_t cutoff = ui_renderer_template_value_to_u7(cutoff_value);
    const uint8_t resonance = ui_renderer_template_value_to_u7(resonance_value);
    const int left = x + 2;
    const int right = x + w - 3;
    const int top = y + 2;
    const int bottom = y + h - 3;
    const int floor = bottom - 3;
    const int high = top + 5;
    const int lp_hp_high = high + 3;
    const int x_mid = left + ((right - left) / 2);
    const int cutoff_x = ui_renderer_template_clamp_i32(left + 6 + (((right - left - 12) * (int)cutoff) / 127), left + 6, right - 6);
    const int lp_hp_peak = ui_renderer_template_clamp_i32(lp_hp_high - (((lp_hp_high - top - 1) * (int)resonance) / 127), top + 1, lp_hp_high);

    drv_display_draw_line(left, bottom, right, bottom);

    switch (filter_type)
    {
        case MIXER_TRACK_FILTER_EQ3:
        {
            const int band_w = (right - left) / 3;
            const int x1 = left + band_w;
            const int x2 = right - band_w;
            const int y_left = high + (((floor - high) * (int)cutoff) / 127);
            const int y_right = high + (((floor - high) * (127 - (int)cutoff)) / 127);
            const int y_mid = ui_renderer_template_clamp_i32(floor - 2 - (((floor - top - 4) * (int)resonance) / 127),
                                                             top + 2,
                                                             floor - 2);
            ui_renderer_template_draw_filter_curve_segment(left, y_left, x1, y_left, bottom);
            ui_renderer_template_draw_filter_curve_segment(x1, y_left, x_mid, y_mid, bottom);
            ui_renderer_template_draw_filter_curve_segment(x_mid, y_mid, x2, y_right, bottom);
            ui_renderer_template_draw_filter_curve_segment(x2, y_right, right, y_right, bottom);
            break;
        }

        case MIXER_TRACK_FILTER_HP_BI:
            ui_renderer_template_draw_filter_curve_segment(left, floor, cutoff_x - 5, floor, bottom);
            ui_renderer_template_draw_filter_curve_segment(cutoff_x - 5, floor, cutoff_x, lp_hp_peak, bottom);
            ui_renderer_template_draw_filter_curve_segment(cutoff_x, lp_hp_peak, cutoff_x + 5, lp_hp_high, bottom);
            ui_renderer_template_draw_filter_curve_segment(cutoff_x + 5, lp_hp_high, right, lp_hp_high, bottom);
            break;

        case MIXER_TRACK_FILTER_BP_BI:
        {
            const int half_w = 16 - (((int)resonance * 8) / 127);
            const int band_left = ui_renderer_template_clamp_i32(cutoff_x - half_w, left, right);
            const int band_right = ui_renderer_template_clamp_i32(cutoff_x + half_w, left, right);
            const int peak = ui_renderer_template_clamp_i32(floor - 3 - (((floor - top - 5) * (int)resonance) / 127),
                                                            top + 2,
                                                            floor - 3);
            ui_renderer_template_draw_filter_curve_segment(left, floor, band_left, floor, bottom);
            ui_renderer_template_draw_filter_curve_segment(band_left, floor, cutoff_x, peak, bottom);
            ui_renderer_template_draw_filter_curve_segment(cutoff_x, peak, band_right, floor, bottom);
            ui_renderer_template_draw_filter_curve_segment(band_right, floor, right, floor, bottom);
            break;
        }

        case MIXER_TRACK_FILTER_LP_BI:
        default:
            ui_renderer_template_draw_filter_curve_segment(left, lp_hp_high, cutoff_x - 5, lp_hp_high, bottom);
            ui_renderer_template_draw_filter_curve_segment(cutoff_x - 5, lp_hp_high, cutoff_x, lp_hp_peak, bottom);
            ui_renderer_template_draw_filter_curve_segment(cutoff_x, lp_hp_peak, cutoff_x + 6, floor, bottom);
            ui_renderer_template_draw_filter_curve_segment(cutoff_x + 6, floor, right, floor, bottom);
            break;
    }

    return 1U;
}

static uint8_t ui_renderer_template_draw_custom_filter(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                       ui_template_custom_widget_kind_t kind,
                                                       int x,
                                                       int y,
                                                       int w,
                                                       int h,
                                                       float value,
                                                       uint8_t allow_group_curve)
{
    mixer_track_filter_type_t filter_type = MIXER_TRACK_FILTER_OFF;
    if (ui_renderer_template_filter_type_visible(plock_frame_ctx, &filter_type) == 0U)
    {
        return 0U;
    }

    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_TYPE)
    {
        const char *label = ui_renderer_template_filter_type_short_label(filter_type);
        const font_t *font = (filter_type == MIXER_TRACK_FILTER_OFF) ? &FONT_OFF_COMPACT : &FONT_HELVB14;
        return (label != NULL) ? ui_renderer_template_draw_filter_big_text(x, y, w, h, label, font) : 0U;
    }

    if ((kind == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CUTOFF)
            && (ui_renderer_template_filter_param_supported(ui_get_active_track(), PARAM_FILTER_CUTOFF) == 0U))
    {
        return 0U;
    }
    if ((kind == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_RESONANCE)
            && (ui_renderer_template_filter_param_supported(ui_get_active_track(), PARAM_FILTER_RESONANCE) == 0U))
    {
        return 0U;
    }

    if (filter_type == MIXER_TRACK_FILTER_OFF)
    {
        return ui_renderer_template_draw_filter_text(x, y, w, h, "-", &FONT_4X6, 0);
    }

    if ((kind == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP) && (allow_group_curve != 0U))
    {
        float cutoff_value = 64.0f;
        float resonance_value = 0.0f;
        if ((ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_FILTER_CUTOFF, &cutoff_value, 0) == 0U)
                || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_FILTER_RESONANCE, &resonance_value, 0) == 0U))
        {
            return 0U;
        }
        return ui_renderer_template_draw_filter_group_curve(filter_type, cutoff_value, resonance_value, x, y, w, h);
    }

    (void)value;
    return 0U;
}

static uint8_t ui_renderer_template_custom_adsr_params(ui_template_custom_widget_kind_t kind,
                                                       param_id_t *out_attack,
                                                       param_id_t *out_decay,
                                                       param_id_t *out_sustain,
                                                       param_id_t *out_release)
{
    if ((out_attack == 0) || (out_decay == 0) || (out_sustain == 0) || (out_release == 0))
    {
        return 0U;
    }

    switch (kind)
    {
        case UI_TEMPLATE_CUSTOM_WIDGET_ADSR_FILTER:
            *out_attack = PARAM_FILTER_ATTACK;
            *out_decay = PARAM_FILTER_DECAY;
            *out_sustain = PARAM_FILTER_SUSTAIN;
            *out_release = PARAM_FILTER_RELEASE;
            return 1U;

        case UI_TEMPLATE_CUSTOM_WIDGET_ADSR_VCA:
            *out_attack = PARAM_VCA_ATTACK;
            *out_decay = PARAM_VCA_DECAY;
            *out_sustain = PARAM_VCA_SUSTAIN;
            *out_release = PARAM_VCA_RELEASE;
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t ui_renderer_template_custom_adsr_supported(uint8_t active_track,
                                                          param_id_t attack,
                                                          param_id_t decay,
                                                          param_id_t sustain,
                                                          param_id_t release)
{
    const track_runtime_param_status_t attack_status = track_runtime_get_effective_param_status(active_track, attack);
    const track_runtime_param_status_t decay_status = track_runtime_get_effective_param_status(active_track, decay);
    const track_runtime_param_status_t sustain_status = track_runtime_get_effective_param_status(active_track, sustain);
    const track_runtime_param_status_t release_status = track_runtime_get_effective_param_status(active_track, release);

    return (uint8_t)(((attack_status == TRACK_RUNTIME_PARAM_ALLOWED) || (attack_status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
            && ((decay_status == TRACK_RUNTIME_PARAM_ALLOWED) || (decay_status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
            && ((sustain_status == TRACK_RUNTIME_PARAM_ALLOWED) || (sustain_status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
            && ((release_status == TRACK_RUNTIME_PARAM_ALLOWED) || (release_status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)));
}

static uint8_t ui_renderer_template_prepare_custom_adsr(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                        ui_template_custom_widget_kind_t kind,
                                                        ui_renderer_template_adsr_shape_t *out_shape)
{
    if (out_shape == 0)
    {
        return 0U;
    }

    param_id_t attack_param = PARAM_COUNT;
    param_id_t decay_param = PARAM_COUNT;
    param_id_t sustain_param = PARAM_COUNT;
    param_id_t release_param = PARAM_COUNT;
    if (ui_renderer_template_custom_adsr_params(kind, &attack_param, &decay_param, &sustain_param, &release_param) == 0U)
    {
        return 0U;
    }

    if (ui_renderer_template_custom_adsr_supported(ui_get_active_track(), attack_param, decay_param, sustain_param, release_param) == 0U)
    {
        return 0U;
    }

    float attack_value = 0.0f;
    float decay_value = 0.0f;
    float sustain_value = 0.0f;
    float release_value = 0.0f;
    if ((ui_renderer_template_get_visible_param_value(plock_frame_ctx, attack_param, &attack_value, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, decay_param, &decay_value, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, sustain_param, &sustain_value, 0) == 0U)
            || (ui_renderer_template_get_visible_param_value(plock_frame_ctx, release_param, &release_value, 0) == 0U))
    {
        return 0U;
    }

    const uint8_t attack = ui_renderer_template_value_to_u7(attack_value);
    const uint8_t decay = ui_renderer_template_value_to_u7(decay_value);
    const uint8_t sustain = ui_renderer_template_value_to_u7(sustain_value);
    const uint8_t release = ui_renderer_template_value_to_u7(release_value);

    out_shape->attack = attack;
    out_shape->decay = decay;
    out_shape->sustain = sustain;
    out_shape->release = release;
    out_shape->locked[0] = ui_macro_interaction_param_is_locked(attack_param);
    out_shape->locked[1] = ui_macro_interaction_param_is_locked(decay_param);
    out_shape->locked[2] = ui_macro_interaction_param_is_locked(sustain_param);
    out_shape->locked[3] = ui_macro_interaction_param_is_locked(release_param);
    return 1U;
}

static int ui_renderer_template_lerp_i32(int x, int x0, int y0, int x1, int y1)
{
    if (x1 == x0)
    {
        return y1;
    }

    const int dx = x1 - x0;
    const int dy = y1 - y0;
    const int num = (dy * (x - x0));
    if (num >= 0)
    {
        return y0 + ((num + (dx / 2)) / dx);
    }
    return y0 + ((num - (dx / 2)) / dx);
}

static uint8_t ui_renderer_template_slot_for_x(int x)
{
    if (x < 32)
    {
        return 0U;
    }
    if (x < 64)
    {
        return 1U;
    }
    if (x < 96)
    {
        return 2U;
    }
    return 3U;
}

static int ui_renderer_template_adsr_y_for_x(int x,
                                             int x0,
                                             int x3,
                                             int x4,
                                             int ax,
                                             int dx,
                                             int rx,
                                             int peak_y,
                                             int sustain_y,
                                             int bottom)
{
    if (x <= ax)
    {
        return ui_renderer_template_lerp_i32(x, x0, bottom, ax, peak_y);
    }
    if (x <= dx)
    {
        return ui_renderer_template_lerp_i32(x, ax, peak_y, dx, sustain_y);
    }
    if (x <= x3)
    {
        return sustain_y;
    }
    if (x <= rx)
    {
        return ui_renderer_template_lerp_i32(x, x3, sustain_y, rx, bottom);
    }
    (void)x4;
    return bottom;
}

static uint8_t ui_renderer_template_draw_custom_adsr_shape(const ui_renderer_template_adsr_shape_t *shape,
                                                           int x,
                                                           int y,
                                                           int w,
                                                           int h,
                                                           uint8_t segment_by_slot_lock)
{
    if ((shape == 0) || (w < 8) || (h < 8))
    {
        return 0U;
    }

    const int left = x + 2;
    const int right = x + w - 3;
    const int top = y + 2;
    const int bottom = y + h - 3;
    if ((right <= left) || (bottom <= top))
    {
        return 0U;
    }

    const int plot_w = right - left;
    const int zone_w = plot_w / 4;
    if (zone_w < 3)
    {
        return 0U;
    }

    const int amp_h = bottom - top;
    const int x0 = left;
    const int x1 = left + zone_w;
    const int x2 = left + (2 * zone_w);
    const int x3 = left + (3 * zone_w);
    const int x4 = right;
    const int peak_y = top + 1;
    const int sustain_y = ui_renderer_template_clamp_i32((bottom - 1) - (((amp_h - 2) * (int)shape->sustain) / 127), top + 1, bottom - 1);
    const int attack_x = ui_renderer_template_clamp_i32(x0 + 1 + (((zone_w - 2) * (int)shape->attack) / 127), x0 + 1, x1 - 1);
    const int decay_x = ui_renderer_template_clamp_i32(x1 + 1 + (((zone_w - 2) * (int)shape->decay) / 127), x1 + 1, x2 - 1);
    const int release_x = ui_renderer_template_clamp_i32(x3 + 1 + (((x4 - x3 - 2) * (int)shape->release) / 127), x3 + 1, x4 - 1);

    if (segment_by_slot_lock == 0U)
    {
        drv_display_draw_line(x0, bottom, attack_x, peak_y);
        drv_display_draw_line(attack_x, peak_y, decay_x, sustain_y);
        drv_display_draw_line(decay_x, sustain_y, x3, sustain_y);
        drv_display_draw_line(x3, sustain_y, release_x, bottom);
        drv_display_draw_line(release_x, bottom, x4, bottom);

        if ((bottom - top) >= 10)
        {
            drv_display_draw_pixel(x0, bottom, true);
            drv_display_draw_pixel(x4, bottom, true);
        }

        return 1U;
    }

    int prev_x = x0;
    int prev_y = bottom;
    for (int draw_x = x0 + 1; draw_x <= x4; ++draw_x)
    {
        const int draw_y = ui_renderer_template_adsr_y_for_x(draw_x,
                                                             x0,
                                                             x3,
                                                             x4,
                                                             attack_x,
                                                             decay_x,
                                                             release_x,
                                                             peak_y,
                                                             sustain_y,
                                                             bottom);
        const uint8_t slot = ui_renderer_template_slot_for_x(draw_x);
        drv_display_set_draw_color((shape->locked[slot] != 0U) ? 0U : 1U);
        drv_display_draw_line(prev_x, prev_y, draw_x, draw_y);
        prev_x = draw_x;
        prev_y = draw_y;
    }

    if ((bottom - top) >= 10)
    {
        drv_display_set_draw_color((shape->locked[ui_renderer_template_slot_for_x(x0)] != 0U) ? 0U : 1U);
        drv_display_draw_pixel(x0, bottom, true);
        drv_display_set_draw_color((shape->locked[ui_renderer_template_slot_for_x(x4)] != 0U) ? 0U : 1U);
        drv_display_draw_pixel(x4, bottom, true);
    }

    drv_display_set_draw_color(1U);
    return 1U;
}

static uint8_t ui_renderer_template_draw_custom_adsr(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                     ui_template_custom_widget_kind_t kind,
                                                     int x,
                                                     int y,
                                                     int w,
                                                     int h)
{
    ui_renderer_template_adsr_shape_t shape;
    if (ui_renderer_template_prepare_custom_adsr(plock_frame_ctx, kind, &shape) == 0U)
    {
        return 0U;
    }

    (void)ui_renderer_template_draw_custom_adsr_shape(&shape, x, y, w, h, 0U);
    return 1U;
}

static ui_template_custom_widget_kind_t ui_renderer_template_resolve_custom_widget(const ui_template_page_state_t *state,
                                                                                   const ui_template_subpage_t *subpage,
                                                                                   uint8_t slot,
                                                                                   param_id_t id)
{
    if ((state == 0) || (state->custom_widget_picker == 0))
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    return state->custom_widget_picker(slot, subpage, id);
}

static ui_template_custom_widget_kind_t ui_renderer_template_resolve_grouped_custom_widget(const ui_template_page_state_t *state,
                                                                                           const ui_template_subpage_t *subpage)
{
    if (subpage == 0)
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    const ui_template_custom_widget_kind_t kind =
        ui_renderer_template_resolve_custom_widget(state, subpage, 0U, subpage->param_bank.params[0]);
    if (kind == UI_TEMPLATE_CUSTOM_WIDGET_NONE)
    {
        return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
    }

    for (uint8_t slot = 1U; slot < 4U; ++slot)
    {
        if (ui_renderer_template_resolve_custom_widget(state, subpage, slot, subpage->param_bank.params[slot]) != kind)
        {
            return UI_TEMPLATE_CUSTOM_WIDGET_NONE;
        }
    }

    return kind;
}

static uint8_t ui_renderer_template_filter_curve_group_is_active(const ui_template_page_state_t *state,
                                                                 const ui_template_subpage_t *subpage,
                                                                 const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx)
{
    if ((subpage == 0)
            || (subpage->param_bank.params[UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST] != PARAM_FILTER_CUTOFF)
            || (subpage->param_bank.params[UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST + 1U] != PARAM_FILTER_RESONANCE)
            || (ui_renderer_template_resolve_custom_widget(state,
                                                           subpage,
                                                           UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST,
                                                           PARAM_FILTER_CUTOFF) != UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP)
            || (ui_renderer_template_resolve_custom_widget(state,
                                                           subpage,
                                                           UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST + 1U,
                                                           PARAM_FILTER_RESONANCE) != UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP))
    {
        return 0U;
    }

    mixer_track_filter_type_t filter_type = MIXER_TRACK_FILTER_OFF;
    if (ui_renderer_template_filter_type_visible(plock_frame_ctx, &filter_type) == 0U)
    {
        return 0U;
    }
    if (filter_type == MIXER_TRACK_FILTER_OFF)
    {
        return 0U;
    }
    if ((ui_renderer_template_filter_param_supported(ui_get_active_track(), PARAM_FILTER_CUTOFF) == 0U)
            || (ui_renderer_template_filter_param_supported(ui_get_active_track(), PARAM_FILTER_RESONANCE) == 0U))
    {
        return 0U;
    }
    return 1U;
}

static uint8_t ui_renderer_template_draw_filter_curve_group(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx)
{
    const int x = g_ui_template_frame_x[UI_TEMPLATE_FILTER_GROUP_SLOT_FIRST] + UI_TEMPLATE_CARD_WIDGET_X_PAD;
    const int y = UI_TEMPLATE_FRAME_Y + UI_TEMPLATE_CARD_WIDGET_Y;
    const int w = (UI_TEMPLATE_FRAME_W * UI_TEMPLATE_FILTER_GROUP_SLOT_COUNT) - (2 * UI_TEMPLATE_CARD_WIDGET_X_PAD);
    const int h = UI_TEMPLATE_CARD_WIDGET_H;
    return ui_renderer_template_draw_custom_filter(plock_frame_ctx,
                                                   UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP,
                                                   x,
                                                   y,
                                                   w,
                                                   h,
                                                   0.0f,
                                                   1U);
}

static uint8_t ui_renderer_template_is_sampler_ram_tone(const ui_template_page_state_t *state,
                                                        const ui_template_family_t *family)
{
    const uint8_t active_track = ui_get_active_track();

    if ((state == NULL) || (family == NULL) || (family->family_title == NULL))
    {
        return 0U;
    }
    if (strcmp(family->family_title, "TONE") != 0)
    {
        return 0U;
    }

    return (uint8_t)(((ui_get_track_family(active_track) == UI_TRACK_FAMILY_SAMPLER)
                      && (ui_get_track_type(active_track) == UI_TRACK_TYPE_SAMPLER)) ? 1U : 0U);
}

static const char *ui_renderer_template_path_basename(const char *path)
{
    const char *base = path;

    if (path == NULL)
    {
        return NULL;
    }

    for (const char *p = path; *p != '\0'; ++p)
    {
        if ((*p == '/') || (*p == '\\') || (*p == ':'))
        {
            base = p + 1;
        }
    }

    return base;
}

static uint16_t ui_renderer_template_sampler_ram_selected_global_slot(void)
{
    const float sample_value = ui_renderer_template_get_param_display_value(PARAM_SAMPLER_SAMPLE);
    uint16_t sample_index = 0U;
    if (sample_value > 0.0f)
    {
        sample_index = (uint16_t)(sample_value + 0.5f);
    }
    return sample_index;
}

static void ui_renderer_template_sampler_ram_sample_label(char *out, uint32_t out_len)
{
    if ((out == NULL) || (out_len == 0U))
    {
        return;
    }

    (void)snprintf(out, out_len, "NO SAMPLE");

    const uint16_t sample_index = ui_renderer_template_sampler_ram_selected_global_slot();
    const sample_global_slot_t *const sample_slot = sample_global_pool_get_slot(sample_index);
    if ((sample_slot == NULL)
            || (sample_slot->kind != SAMPLE_GLOBAL_KIND_RAM)
            || (sample_slot->state != SAMPLE_GLOBAL_STATE_READY)
            || (sample_slot->path[0] == '\0'))
    {
        return;
    }

    const char *const base = ui_renderer_template_path_basename(sample_slot->path);
    if ((base == NULL) || (base[0] == '\0'))
    {
        return;
    }

    uint32_t i = 0U;
    while ((base[i] != '\0') && (base[i] != '.') && ((i + 1U) < out_len))
    {
        out[i] = base[i];
        i++;
    }
    out[i] = '\0';
    ui_renderer_template_fit_text(out, 96U);
}

static int ui_renderer_template_sampler_ram_amp_to_y(int16_t value,
                                                     uint16_t peak,
                                                     int top,
                                                     int inner_h)
{
    const int center = top + (inner_h / 2);
    const int half = (inner_h > 1) ? ((inner_h - 1) / 2) : 0;
    if ((peak == 0U) || (half == 0))
    {
        return center;
    }

    int y = center - (int)(((int32_t)value * half) / (int32_t)peak);
    const int bottom = top + inner_h - 1;
    if (y < top)
    {
        y = top;
    }
    else if (y > bottom)
    {
        y = bottom;
    }
    return y;
}

static int ui_renderer_template_sampler_ram_marker_x(float value,
                                                     uint32_t total_frames,
                                                     int inner_x,
                                                     int inner_w)
{
    if ((total_frames == 0U) || (inner_w <= 0))
    {
        return -1;
    }
    if (value < 0.0f)
    {
        value = 0.0f;
    }
    else if (value > 1.0f)
    {
        value = 1.0f;
    }

    const uint32_t max_frame = (total_frames > 1U) ? (total_frames - 1U) : 0U;
    uint32_t frame = (uint32_t)((value * (float)max_frame) + 0.5f);
    if (frame > max_frame)
    {
        frame = max_frame;
    }
    if (max_frame == 0U)
    {
        return inner_x;
    }
    return inner_x + (int)(((uint64_t)frame * (uint32_t)(inner_w - 1)) / max_frame);
}

static void ui_renderer_template_draw_sampler_ram_marker_label(int x, char label)
{
    char txt[2] = { label, '\0' };
    const uint8_t text_w = drv_display_text_width(txt);
    const int label_y = UI_TEMPLATE_SAMPLER_WAVE_Y + UI_TEMPLATE_SAMPLER_WAVE_H;
    int label_x = x - ((int)text_w / 2);
    label_x = ui_renderer_template_clamp_i32(label_x, 0, (int)OLED_WIDTH - (int)text_w);

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text((uint8_t)label_x, (uint8_t)label_y, txt);
}

static void ui_renderer_template_draw_sampler_ram_marker_line(int x,
                                                              int inner_y,
                                                              int inner_h,
                                                              uint8_t dotted)
{
    if (dotted != 0U)
    {
        const int bottom = inner_y + inner_h - 1;
        for (int y = inner_y; y <= bottom; y += 4)
        {
            const int y1 = ((y + 1) <= bottom) ? (y + 1) : bottom;
            drv_display_draw_line(x, y, x, y1);
        }
        return;
    }

    drv_display_draw_line(x, inner_y, x, inner_y + inner_h - 1);
}

static void ui_renderer_template_draw_sampler_ram_marker(float value,
                                                         uint32_t total_frames,
                                                         int inner_x,
                                                         int inner_y,
                                                         int inner_w,
                                                         int inner_h,
                                                         char label,
                                                         uint8_t dotted)
{
    const int x = ui_renderer_template_sampler_ram_marker_x(value, total_frames, inner_x, inner_w);
    if (x < 0)
    {
        return;
    }

    drv_display_set_draw_color(2U);
    ui_renderer_template_draw_sampler_ram_marker_line(x, inner_y, inner_h, dotted);
    drv_display_set_draw_color(1U);
    ui_renderer_template_draw_sampler_ram_marker_label(x, label);
}

static uint16_t ui_renderer_template_sampler_ram_slice_divisions(float slice_value)
{
    static const uint16_t k_slice_counts[] = {1U, 2U, 4U, 8U, 16U, 32U, 64U};
    int32_t index = (int32_t)(slice_value + 0.5f);
    if (index < 0)
    {
        index = 0;
    }
    if (index >= (int32_t)(sizeof(k_slice_counts) / sizeof(k_slice_counts[0])))
    {
        index = (int32_t)((sizeof(k_slice_counts) / sizeof(k_slice_counts[0])) - 1U);
    }
    return k_slice_counts[index];
}

static void ui_renderer_template_draw_sampler_ram_slice_divisions(uint16_t divisions,
                                                                  float start_value,
                                                                  float end_value,
                                                                  uint32_t frame_count,
                                                                  int inner_x,
                                                                  int inner_y,
                                                                  int inner_w,
                                                                  int inner_h)
{
    if ((divisions <= 1U) || (inner_w <= 1) || (inner_h <= 2))
    {
        return;
    }

    if (start_value < 0.0f)
    {
        start_value = 0.0f;
    }
    else if (start_value > 1.0f)
    {
        start_value = 1.0f;
    }
    if (end_value < 0.0f)
    {
        end_value = 0.0f;
    }
    else if (end_value > 1.0f)
    {
        end_value = 1.0f;
    }

    uint32_t region_begin = 0U;
    uint32_t region_end = frame_count;
    if (frame_count != 0U)
    {
        region_begin = (uint32_t)(start_value * (float)frame_count);
        region_end = (uint32_t)(end_value * (float)frame_count);
        if ((region_end == 0U) || (region_end > frame_count))
        {
            region_end = frame_count;
        }
        if (region_begin >= frame_count)
        {
            region_begin = frame_count - 1U;
        }
        if (region_end <= region_begin)
        {
            region_begin = 0U;
            region_end = frame_count;
        }
    }
    const uint32_t region_frames = region_end - region_begin;
    const int bottom = inner_y + inner_h - 2;
    drv_display_set_draw_color(2U);
    for (uint16_t i = 1U; i < divisions; ++i)
    {
        const uint32_t frame =
            region_begin + (uint32_t)(((uint64_t)region_frames * (uint64_t)i)
                                      / (uint64_t)divisions);
        const int x = inner_x + (int)(((uint64_t)frame * (uint32_t)(inner_w - 1))
                                      / (uint64_t)frame_count);
        if ((x <= inner_x) || (x >= (inner_x + inner_w)))
        {
            continue;
        }
        for (int y = inner_y + 1; y <= bottom; y += 5)
        {
            drv_display_draw_pixel(x, y, true);
        }
    }
    drv_display_set_draw_color(1U);
}

static int ui_renderer_template_sampler_ram_frame_to_x(uint32_t frame,
                                                       uint32_t frame_count,
                                                       int inner_x,
                                                       int inner_w)
{
    if ((frame_count == 0U) || (inner_w <= 0))
    {
        return -1;
    }
    if (frame >= frame_count)
    {
        frame = frame_count - 1U;
    }
    return inner_x + (int)(((uint64_t)frame * (uint32_t)(inner_w - 1)) / frame_count);
}

static void ui_renderer_template_draw_sampler_ram_playhead(uint16_t global_slot,
                                                           uint32_t frame_count,
                                                           int inner_x,
                                                           int inner_y,
                                                           int inner_w,
                                                           int inner_h)
{
    brick6_sampler_ram_playhead_snapshot_t playhead;
    if ((brick6_sampler_runtime_get_ram_playhead(ui_get_active_track(),
                                                 global_slot,
                                                 &playhead) == 0U)
        || (playhead.active == 0U)
        || (playhead.frame_count == 0U))
    {
        return;
    }

    const uint32_t scale_frames = (frame_count != 0U) ? frame_count : playhead.frame_count;
    const int x = ui_renderer_template_sampler_ram_frame_to_x(playhead.frame,
                                                              scale_frames,
                                                              inner_x,
                                                              inner_w);
    if (x < 0)
    {
        return;
    }

    const int bottom = inner_y + inner_h - 1;
    drv_display_set_draw_color(2U);
    for (int y = inner_y; y <= bottom; y += 3)
    {
        drv_display_draw_pixel(x, y, true);
    }
    drv_display_set_draw_color(1U);
}

static void ui_renderer_template_draw_sampler_ram_waveform(const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx)
{
    const int wave_x = UI_TEMPLATE_SAMPLER_WAVE_X;
    const int wave_y = UI_TEMPLATE_SAMPLER_WAVE_Y;
    const int wave_w = UI_TEMPLATE_SAMPLER_WAVE_W;
    const int wave_h = UI_TEMPLATE_SAMPLER_WAVE_H;
    const int inner_x = wave_x + 1;
    const int inner_y = wave_y + 1;
    const int inner_w = UI_TEMPLATE_SAMPLER_WAVE_INNER_W;
    const int inner_h = UI_TEMPLATE_SAMPLER_WAVE_INNER_H;
    const int center_y = inner_y + (inner_h / 2);
    const uint16_t global_slot = ui_renderer_template_sampler_ram_selected_global_slot();
    const sample_ram_waveform_overview_t *const overview =
        sampler_ram_pool_get_waveform_for_global(global_slot);

    drv_display_draw_rect(wave_x, wave_y, wave_w, wave_h);

    for (int x = inner_x; x < (inner_x + inner_w); x += 2)
    {
        drv_display_draw_pixel(x, center_y, true);
    }

    if (overview == NULL)
    {
        return;
    }
    if (!((overview->state == SAMPLE_RAM_WAVEFORM_READY)
          || (overview->state == SAMPLE_RAM_WAVEFORM_BUILDING))
        || (overview->columns == 0U)
        || (overview->frame_count == 0U))
    {
        return;
    }

    const uint16_t peak = overview->global_peak;
    if (peak > 1U)
    {
        uint16_t columns = (overview->state == SAMPLE_RAM_WAVEFORM_READY)
                               ? overview->columns
                               : overview->ready_columns;
        if (columns > (uint16_t)inner_w)
        {
            columns = (uint16_t)inner_w;
        }
        for (uint16_t col = 0U; col < columns; ++col)
        {
            const int y_top = ui_renderer_template_sampler_ram_amp_to_y(overview->max[col],
                                                                        peak,
                                                                        inner_y,
                                                                        inner_h);
            const int y_bottom = ui_renderer_template_sampler_ram_amp_to_y(overview->min[col],
                                                                           peak,
                                                                           inner_y,
                                                                           inner_h);
            drv_display_draw_line(inner_x + (int)col, y_top, inner_x + (int)col, y_bottom);
        }
    }

    float start_value = 0.0f;
    float end_value = 1.0f;
    float loop_value = 0.0f;
    float slice_value = 0.0f;
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_SAMPLER_START, &start_value, 0);
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_SAMPLER_END, &end_value, 0);
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_SAMPLER_LOOP_START, &loop_value, 0);
    (void)ui_renderer_template_get_visible_param_value(plock_frame_ctx, PARAM_SAMPLER_SLICE_COUNT, &slice_value, 0);
    ui_renderer_template_draw_sampler_ram_slice_divisions(
        ui_renderer_template_sampler_ram_slice_divisions(slice_value),
        start_value,
        end_value,
        overview->frame_count,
        inner_x,
        inner_y,
        inner_w,
        inner_h);
    ui_renderer_template_draw_sampler_ram_playhead(global_slot,
                                                  overview->frame_count,
                                                  inner_x,
                                                  inner_y,
                                                  inner_w,
                                                  inner_h);
    ui_renderer_template_draw_sampler_ram_marker(start_value,
                                                 overview->frame_count,
                                                 inner_x,
                                                 inner_y,
                                                 inner_w,
                                                 inner_h,
                                                 'S',
                                                 0U);
    ui_renderer_template_draw_sampler_ram_marker(end_value,
                                                 overview->frame_count,
                                                 inner_x,
                                                 inner_y,
                                                 inner_w,
                                                 inner_h,
                                                 'E',
                                                 0U);
    if (brick6_sampler_runtime_ram_slice_mode_active(ui_get_active_track()) == 0U)
    {
        ui_renderer_template_draw_sampler_ram_marker(loop_value,
                                                     overview->frame_count,
                                                     inner_x,
                                                     inner_y,
                                                     inner_w,
                                                     inner_h,
                                                     'L',
                                                     1U);
    }
}

static void ui_renderer_template_draw_sampler_ram_slot_text(const ui_template_page_state_t *state,
                                                           const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                           uint8_t slot,
                                                           param_id_t id)
{
    const int x = g_ui_template_frame_x[slot];
    char name_txt[24] = "-";
    char value_txt[20] = "---";
    char bottom_txt[24] = "-";
    uint8_t draw_name_inverted = 0U;
    float value = 0.0f;

    (void)ui_renderer_template_prepare_param_slot_texts(state,
                                                        plock_frame_ctx,
                                                        slot,
                                                        id,
                                                        &value,
                                                        &draw_name_inverted,
                                                        name_txt,
                                                        (uint32_t)sizeof(name_txt),
                                                        value_txt,
                                                        (uint32_t)sizeof(value_txt),
                                                        bottom_txt,
                                                        (uint32_t)sizeof(bottom_txt),
                                                        NULL);
    (void)value;

    drv_display_set_font(&FONT_4X6);
    ui_renderer_template_fit_text(bottom_txt, UI_TEMPLATE_SAMPLER_TEXT_MAX_PX);

    const uint8_t text_x = (uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, bottom_txt);
    drv_display_draw_text(text_x, UI_TEMPLATE_SAMPLER_LABEL_Y, bottom_txt);

    if ((draw_name_inverted != 0U) && (drv_display_text_width(bottom_txt) != 0U))
    {
        const uint8_t text_w = drv_display_text_width(bottom_txt);
        drv_display_draw_line(text_x,
                              UI_TEMPLATE_SAMPLER_LABEL_Y + 6,
                              text_x + text_w - 1U,
                              UI_TEMPLATE_SAMPLER_LABEL_Y + 6);
    }
}

static void ui_renderer_template_draw_sampler_ram_wave_placeholder(const ui_template_page_state_t *state,
                                                                   const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                                   const ui_template_subpage_t *subpage)
{
    char sample_label[40];
    ui_renderer_template_sampler_ram_sample_label(sample_label, (uint32_t)sizeof(sample_label));

    drv_display_set_font(&FONT_4X6);
    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(0, OLED_WIDTH, sample_label),
                          UI_TEMPLATE_SAMPLER_NAME_Y,
                          sample_label);
    ui_renderer_template_draw_sampler_ram_waveform(plock_frame_ctx);

    if (subpage == NULL)
    {
        return;
    }

    for (uint8_t slot = 0U; slot < 4U; ++slot)
    {
        ui_renderer_template_draw_sampler_ram_slot_text(state,
                                                        plock_frame_ctx,
                                                        slot,
                                                        subpage->param_bank.params[slot]);
    }
}

static void ui_renderer_template_draw_param_slot(const ui_template_page_state_t *state,
                                                 const ui_param_seq_plock_feedback_frame_t *plock_frame_ctx,
                                                 const ui_template_subpage_t *subpage,
                                                 ui_template_custom_widget_kind_t grouped_widget,
                                                 uint8_t grouped_widget_drawn,
                                                 uint8_t slot,
                                                 param_id_t id)
{
    const int x = g_ui_template_frame_x[slot];
    const int y = UI_TEMPLATE_FRAME_Y;
    const int widget_x = x + UI_TEMPLATE_CARD_WIDGET_X_PAD;
    const int widget_y = y + UI_TEMPLATE_CARD_WIDGET_Y;
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
                uiw_draw_enum_text(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, virt_value);
                drv_display_set_font(&FONT_4X6);
                drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, virt_name),
                                      (uint8_t)(y + UI_TEMPLATE_CARD_LABEL_Y),
                                      virt_name);
                return;
            }
        }

        drv_display_set_font(&FONT_4X6);
        drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, "-"), (uint8_t)(y + 14), "-");
        return;
    }

    const param_desc_t *desc = &param_registry[id];
    float value = 0.0f;
    uint8_t draw_name_inverted = 0U;
    const char *enum_label = NULL;
    char value_txt[20];
    char name_txt[24];
    char bottom_txt[24];
    uint8_t flash_active = 0U;

    if (ui_renderer_template_prepare_param_slot_texts(state,
                                                      plock_frame_ctx,
                                                      slot,
                                                      id,
                                                      &value,
                                                      &draw_name_inverted,
                                                      name_txt,
                                                      (uint32_t)sizeof(name_txt),
                                                      value_txt,
                                                      (uint32_t)sizeof(value_txt),
                                                      bottom_txt,
                                                      (uint32_t)sizeof(bottom_txt),
                                                      &flash_active) == 0U)
    {
        return;
    }

    if ((desc->display_type == PARAM_DISPLAY_ENUM) && (desc->labels != NULL))
    {
        const int32_t index = (int32_t)(value + 0.5f);
        if (index >= 0)
        {
            enum_label = desc->labels[index];
        }
    }

    drv_display_set_font(&FONT_4X6);
    ui_renderer_template_fit_text(value_txt, UI_TEMPLATE_CARD_LABEL_MAX_PX);
    const ui_template_custom_widget_kind_t custom_widget =
        ui_renderer_template_resolve_custom_widget(state, subpage, slot, id);
    if ((flash_active == 0U) && (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP))
    {
        mixer_track_filter_type_t filter_type = MIXER_TRACK_FILTER_OFF;
        if ((ui_renderer_template_filter_type_visible(plock_frame_ctx, &filter_type) != 0U)
                && (filter_type == MIXER_TRACK_FILTER_OFF))
        {
            bottom_txt[0] = '\0';
        }
    }
    if ((flash_active == 0U) && (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_INACTIVE))
    {
        bottom_txt[0] = '\0';
    }
    ui_renderer_template_fit_text(bottom_txt, UI_TEMPLATE_CARD_LABEL_MAX_PX);

    if (custom_widget != UI_TEMPLATE_CUSTOM_WIDGET_NONE)
    {
        if (custom_widget == grouped_widget)
        {
            if (grouped_widget_drawn != 0U)
            {
                goto draw_bottom_label;
            }
        }
        else
        {
        if (slot_locked != 0U)
        {
            drv_display_set_draw_color(0U);
        }
        if ((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_PLAY_NOTE)
                ? (ui_renderer_template_draw_play_note_text(id,
                                                            widget_x,
                                                            widget_y,
                                                            UI_TEMPLATE_CARD_WIDGET_W,
                                                            UI_TEMPLATE_CARD_WIDGET_H,
                                                            value) != 0U)
                : ((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_LFO_DEST)
                ? (ui_renderer_template_draw_lfo_dest_text(widget_x,
                                                           widget_y,
                                                           UI_TEMPLATE_CARD_WIDGET_W,
                                                           UI_TEMPLATE_CARD_WIDGET_H,
                                                           value) != 0U)
                : (((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TRACK)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_TYPE)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_INACTIVE)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_CHANNEL)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_TRACK_CFG_MIDI_SOURCE))
                ? (ui_renderer_template_draw_custom_track_cfg(plock_frame_ctx,
                                                              custom_widget,
                                                              widget_x,
                                                              widget_y,
                                                              UI_TEMPLATE_CARD_WIDGET_W,
                                                              UI_TEMPLATE_CARD_WIDGET_H,
                                                              value) != 0U)
                : (((custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_TYPE)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CUTOFF)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_RESONANCE)
                    || (custom_widget == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP))
                ? (ui_renderer_template_draw_custom_filter(plock_frame_ctx,
                                                           custom_widget,
                                                           widget_x,
                                                           widget_y,
                                                           UI_TEMPLATE_CARD_WIDGET_W,
                                                           UI_TEMPLATE_CARD_WIDGET_H,
                                                           value,
                                                           0U) != 0U)
                : (ui_renderer_template_draw_custom_adsr(plock_frame_ctx,
                                                         custom_widget,
                                                         widget_x,
                                                         widget_y,
                                                         UI_TEMPLATE_CARD_WIDGET_W,
                                                         UI_TEMPLATE_CARD_WIDGET_H) != 0U)))))
        {
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(0U);
            }
            goto draw_bottom_label;
        }
        if (slot_locked != 0U)
        {
            drv_display_set_draw_color(1U);
        }
        }
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
            uiw_draw_switch(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, (value >= 0.5f) ? 1U : 0U);
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
            uiw_draw_enum_text(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, (enum_label != NULL) ? enum_label : value_txt);
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
            uiw_draw_jack_icon(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H);
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
            uiw_draw_keyboard_icon(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H);
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
            uiw_draw_wave_icon(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, enum_label);
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
            uiw_draw_filter_icon(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, enum_label);
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
            uiw_draw_knob(widget_x, widget_y, UI_TEMPLATE_CARD_WIDGET_W, UI_TEMPLATE_CARD_WIDGET_H, value, desc->min, desc->max);
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            break;
        }
    }

draw_bottom_label:
    if (slot_locked != 0U)
    {
        drv_display_set_draw_color(0U);
    }

    drv_display_draw_text((uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, bottom_txt),
                          (uint8_t)(y + UI_TEMPLATE_CARD_LABEL_Y),
                          bottom_txt);
    if (draw_name_inverted != 0U)
    {
        const uint8_t text_x = (uint8_t)ui_renderer_template_center_x(x, UI_TEMPLATE_FRAME_W, bottom_txt);
        const uint8_t text_w = drv_display_text_width(bottom_txt);
        if (text_w == 0U)
        {
            if (slot_locked != 0U)
            {
                drv_display_set_draw_color(1U);
            }
            return;
        }
        drv_display_draw_line(text_x,
                              y + UI_TEMPLATE_CARD_LABEL_Y + UI_TEMPLATE_CARD_LABEL_H,
                              text_x + text_w - 1,
                              y + UI_TEMPLATE_CARD_LABEL_Y + UI_TEMPLATE_CARD_LABEL_H);
    }

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
        drv_display_set_font(&FONT_5X7);
        const uint8_t bpm_text_w = drv_display_text_width(bpm_label);
        if (bpm_inverted != 0U)
        {
            const uint8_t bpm_box_w = (uint8_t)(bpm_text_w + 2U);
            ui_renderer_template_draw_inverted_label(ui_renderer_template_right_x(0U, bpm_box_w),
                                                     1U,
                                                     bpm_label,
                                                     &FONT_5X7);
        }
        else
        {
            drv_display_draw_text(ui_renderer_template_right_x(0U, bpm_text_w), 1U, bpm_label);
        }
    }
    char pattern_label[6];
    ui_renderer_template_format_active_pattern_label(pattern_label, sizeof(pattern_label));
    drv_display_set_font(&FONT_4X6);
    const uint8_t pattern_x = ui_renderer_template_right_x(0U, drv_display_text_width(pattern_label));
    const uint8_t cpu_text_w = drv_display_text_width(cpu_avg_label);
    uint8_t cpu_x = (uint8_t)(104U - cpu_text_w);
    if ((uint8_t)(cpu_x + cpu_text_w + 1U) > pattern_x)
    {
        cpu_x = (pattern_x > (uint8_t)(cpu_text_w + 1U)) ? (uint8_t)(pattern_x - cpu_text_w - 1U) : 0U;
    }
    drv_display_draw_text(cpu_x, 9U, cpu_avg_label);
    drv_display_draw_text(pattern_x, 9U, pattern_label);
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
        if (ui_renderer_template_is_sampler_ram_tone(state, family) != 0U)
        {
            ui_renderer_template_draw_sampler_ram_wave_placeholder(state, &plock_frame_ctx, subpage);
        }
        else
        {
            ui_renderer_template_adsr_shape_t grouped_adsr_shape;
            ui_template_custom_widget_kind_t grouped_widget =
                ui_renderer_template_resolve_grouped_custom_widget(state, subpage);
            uint8_t grouped_widget_drawn =
                (uint8_t)((grouped_widget != UI_TEMPLATE_CUSTOM_WIDGET_NONE)
                        && (ui_renderer_template_prepare_custom_adsr(&plock_frame_ctx, grouped_widget, &grouped_adsr_shape) != 0U));

            if (ui_renderer_template_filter_curve_group_is_active(state, subpage, &plock_frame_ctx) != 0U)
            {
                grouped_widget = UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP;
                grouped_widget_drawn = 1U;
            }

            for (uint8_t i = 0U; i < 4U; i++)
            {
                ui_renderer_template_draw_param_slot(state,
                                                     &plock_frame_ctx,
                                                     subpage,
                                                     grouped_widget,
                                                     grouped_widget_drawn,
                                                     i,
                                                     subpage->param_bank.params[i]);
            }

            if (grouped_widget_drawn != 0U)
            {
                if (grouped_widget == UI_TEMPLATE_CUSTOM_WIDGET_FILTER_CURVE_GROUP)
                {
                    (void)ui_renderer_template_draw_filter_curve_group(&plock_frame_ctx);
                }
                else
                {
                    (void)ui_renderer_template_draw_custom_adsr_shape(&grouped_adsr_shape,
                                                                      UI_TEMPLATE_GROUP_WIDGET_X,
                                                                      UI_TEMPLATE_GROUP_WIDGET_Y,
                                                                      UI_TEMPLATE_GROUP_WIDGET_W,
                                                                      UI_TEMPLATE_GROUP_WIDGET_H,
                                                                      1U);
                }
            }
        }
    }

    ui_renderer_template_draw_footer(state);

    drv_display_set_font(&FONT_5X7);
}
